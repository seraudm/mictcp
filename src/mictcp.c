#include <mictcp.h>
#include <api/mictcp_core.h>

#define WINDOW_SIZE 100

#define LOSS_RATE 20

#define CLIENT_ALLOWED_LOSS_RATE 5
#define SERVER_ALLOWED_LOSS_RATE 2

#define TIMER 10


mic_tcp_sock mon_socket; //Socket used for the communication

int index_window;
char *loss_rate_window; //1 is a loss, 0 is a success
char allowed_rate_loss; //it's a percentage, the server and client value can be different and are defined above

pthread_mutex_t mutex_sync = PTHREAD_MUTEX_INITIALIZER; 
pthread_cond_t cond = PTHREAD_COND_INITIALIZER; //Used to synchronize the app thread and the receiving thread for the server

char PE = 0;
char PA = 0;

/*
 * Allocate the memory for the window and initialize is with losses (value 1) to ensure the first images will not be lost
 */
void initialize_window(){
    index_window = 0;
    loss_rate_window = malloc(sizeof(char) * WINDOW_SIZE);
    for (int i=0; i< WINDOW_SIZE; i++){
        loss_rate_window[i] = 1;
    }
}

void push_value_window(char value){
    loss_rate_window[index_window] = value;
    index_window = (index_window + 1) % WINDOW_SIZE;
}

char is_loss_allowed(){
    int number_loss_in_window = 1; //we make as if we have added a loss
    for(int i=0; i<WINDOW_SIZE; i++){
        number_loss_in_window += loss_rate_window[i];
    }
    number_loss_in_window -= loss_rate_window[index_window];
    float loss_rate = ((float)number_loss_in_window)/WINDOW_SIZE;
    printf("nb loss: %d, loss_rate: %f", number_loss_in_window, loss_rate);
    return loss_rate <= (float) allowed_rate_loss/100; 
}


/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

   int result = initialize_components(sm); /* Appel obligatoire */

   set_loss_rate(LOSS_RATE);

   //Initialize the socket with default values that can be changed later with the bind function
   mon_socket.local_addr.ip_addr.addr = "127.0.0.1";
   mon_socket.local_addr.ip_addr.addr_size = strlen(mon_socket.local_addr.ip_addr.addr) + 1;
   mon_socket.local_addr.port = 33000;
   mon_socket.state = IDLE;

   return result;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
   mon_socket.local_addr = addr;
   return 0;
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
    initialize_window(); // Useless if the server isn't sending any data with mic_tcp_send

    allowed_rate_loss = SERVER_ALLOWED_LOSS_RATE;

    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    mon_socket.state = WAIT_FOR_SYN; //The state of the socket is changed so the receiving thread is now waiting for an entering pdu SYN

    if (pthread_mutex_lock(&mutex_sync)){
        printf("Erreur lors du lock\n");
        exit(-1);
    }

    pthread_cond_wait(&cond, &mutex_sync); // To wait for the connection to be established, this is the receiving thread that will release the applicative thread 

    if (pthread_mutex_unlock(&mutex_sync)){
        printf("Erreur lors du unlock\n");
        exit(-1);
    }
    
    printf("Allowed loss rate: %d\n", allowed_rate_loss);
    return 0;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    initialize_window();

    allowed_rate_loss = CLIENT_ALLOWED_LOSS_RATE;

    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

    mon_socket.remote_addr = addr;

    //SENDING THE SYN
    mic_tcp_pdu pdu_syn;
    pdu_syn.header.source_port = mon_socket.local_addr.port;
    pdu_syn.header.dest_port = mon_socket.remote_addr.port;
    pdu_syn.header.syn = 1;

    //To send the allowed rate loss to the server, the data part is used. We use only one byte because it is sufficient for a round percentage.
    pdu_syn.payload.data = malloc(sizeof(char));
    *(pdu_syn.payload.data) = allowed_rate_loss;
    pdu_syn.payload.size = 1;

    IP_send(pdu_syn, mon_socket.remote_addr.ip_addr);

    mic_tcp_pdu pdu_received;
    pdu_received.payload.size = 1;
    pdu_received.payload.data = malloc(sizeof(char));
    mic_tcp_ip_addr local_addr;
    local_addr.addr_size = 16;

    //WAIT FOR THE SYNACK
    while(IP_recv(&pdu_received, &local_addr, &mon_socket.remote_addr.ip_addr, TIMER) == -1 || pdu_received.header.dest_port != mon_socket.local_addr.port ||pdu_received.header.ack == 0 || pdu_received.header.syn == 0){
        IP_send(pdu_syn, mon_socket.remote_addr.ip_addr);
    }

    allowed_rate_loss = *(pdu_received.payload.data); // The final value of the allowed_loss_rate is decided by the server so we need to update it when receiving the synack 

    //SENDING THE LAST ACK OF THE CONNECTION PHASE
    mic_tcp_pdu pdu_ack;
    pdu_ack.header.source_port = mon_socket.local_addr.port;
    pdu_ack.header.dest_port = mon_socket.remote_addr.port;
    pdu_ack.header.ack = 1;
    pdu_ack.payload.size = 0;

    IP_send(pdu_ack, mon_socket.remote_addr.ip_addr);
    

    printf("Final allowed loss rate: %d\n", allowed_rate_loss);
    return 0;
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative;
    pdu_received.payload.size = 0;
    mic_tcp_ip_addr local_addr;
    mic_tcp_ip_addr remote_addr;
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");

    //SENDING THE DATA PDU
    mic_tcp_pdu pdu;
    pdu.payload.data = mesg;
    pdu.payload.size = mesg_size;
    pdu.header.source_port = mon_socket.local_addr.port;
    pdu.header.dest_port = mon_socket.remote_addr.port;
    pdu.header.ack = 0;
    pdu.header.seq_num = PE;
    int size_sent_data = IP_send(pdu, mon_socket.remote_addr.ip_addr);


    mic_tcp_pdu pdu_received;
    pdu_received.payload.size = 0;
    mic_tcp_ip_addr local_addr;
    local_addr.addr_size = 16;

    //WAIT FOR THE ACK
    while(IP_recv(&pdu_received, &local_addr, &mon_socket.remote_addr.ip_addr, TIMER)==-1 || pdu_received.header.dest_port != mon_socket.local_addr.port ||pdu_received.header.ack == 0 || pdu_received.header.ack_num == PE){
        
        //Check if the loss is allowed to know if the retransmission is necessary
        if (is_loss_allowed()){
            printf("\n==========================\nPerte autorisee\n==========================\n");
            push_value_window(1); //add a loss to the window
            return 0;
        }
        printf("\n==========================\nPerte refusee\n==========================\n");
        size_sent_data = IP_send(pdu, mon_socket.remote_addr.ip_addr);
    }
    push_value_window(0); //add a success to the window
    PE = (PE +1) %2;

    return size_sent_data;
}

/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv (int socket, char* mesg, int max_mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    mic_tcp_payload payload;
    payload.data = mesg;
    payload.size = max_mesg_size;
    int size_written_data = app_buffer_get(payload);
    return size_written_data;
}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
    printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");
    //NOT IMPLEMENTED
    return 0;
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_ip_addr local_addr, mic_tcp_ip_addr remote_addr)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    //Check if the pdu received was sent to our socket
    if (pdu.header.dest_port == mon_socket.local_addr.port){

        // Depending of the socket state we are in we react differently
        switch (mon_socket.state){
            case ESTABLISHED:
                //We are waiting for data pdu so not ack
                if (pdu.header.ack == 0){

                    //If the pdu received has the good package number we push it into the receiptive buffer
                    if (pdu.header.seq_num == PA){
                        app_buffer_put(pdu.payload);
                        PA = (PA +1)%2;
                    }

                    //We send an ack to inform which package we are waiting for
                    mic_tcp_pdu pdu_ack;
                    pdu_ack.header.source_port = mon_socket.local_addr.port;
                    pdu_ack.header.dest_port = pdu.header.source_port;
                    pdu_ack.header.ack_num = PA;
                    pdu_ack.header.ack = 1;
                    pdu_ack.payload.size = 0;

                    IP_send(pdu_ack, remote_addr);
                }
                break;

            case WAIT_FOR_SYN:
                if(pdu.header.syn == 1){
                    //We memorize the remote adress
                    mon_socket.remote_addr.ip_addr = remote_addr;
                    mon_socket.remote_addr.port = pdu.header.source_port;

                    mon_socket.state = SYNACK_SENT;
                    

                    //SENDING THE SYNACK PDU
                    mic_tcp_pdu pdu_synack;
                    pdu_synack.header.source_port = mon_socket.local_addr.port;
                    pdu_synack.header.dest_port = mon_socket.remote_addr.port;
                    pdu_synack.header.syn = 1;
                    pdu_synack.header.ack = 1;
                    pdu_synack.payload.data = malloc(sizeof(char));
                    
                    //We read the client allowed_loss_rate and then compare with the one we have defined, the most restrictive is kept
                    char dest_allowed_loss_rate = *(pdu.payload.data);

                    if (allowed_rate_loss < dest_allowed_loss_rate){
                        *(pdu_synack.payload.data) = allowed_rate_loss;
                    } else {
                        allowed_rate_loss = dest_allowed_loss_rate;
                        *(pdu_synack.payload.data) = dest_allowed_loss_rate;
                    }

                    pdu_synack.payload.size = 1;
                    IP_send(pdu_synack, mon_socket.remote_addr.ip_addr);
                    
                }
                break;

            case SYNACK_SENT:
                if (pdu.header.syn == 1){
                    //If we receive another SYN pdu, that means our SYNACK pdu was lost so we send it again
                    mic_tcp_pdu pdu_synack;
                    pdu_synack.header.source_port = mon_socket.local_addr.port;
                    pdu_synack.header.dest_port = mon_socket.remote_addr.port;
                    pdu_synack.header.syn = 1;
                    pdu_synack.header.ack = 1;

                    pdu_synack.payload.data = malloc(sizeof(char));
                    pdu_synack.payload.size = 1;
                    *(pdu_synack.payload.data) = allowed_rate_loss;

                    IP_send(pdu_synack, mon_socket.remote_addr.ip_addr);
                }
                else if (pdu.header.ack == 1){
                    mon_socket.state = ESTABLISHED;
                    pthread_cond_broadcast(&cond); // To release the applicative thread
                }
                break;

            default:
                break;
        }
    }
}
