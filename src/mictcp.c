#include <mictcp.h>
#include <api/mictcp_core.h>

#define WINDOW_SIZE 100

mic_tcp_sock mon_socket;

int index_window;
char *loss_rate_window; //1 is a loss, 0 is a success
char allowed_rate_loss = 0; //it's a percentage

char PE = 0;
char PA = 0;

void initialize_window(){
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
   int result = -1;
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
   result = initialize_components(sm); /* Appel obligatoire */
   set_loss_rate(5);
   mon_socket.local_addr.ip_addr.addr = "127.0.0.1";
   mon_socket.local_addr.ip_addr.addr_size = strlen(mon_socket.local_addr.ip_addr.addr) + 1;
   mon_socket.local_addr.port = 33000;

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
    initialize_window();
    index_window = 0;
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    return 0;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    initialize_window();
    index_window = 0;
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    mon_socket.remote_addr = addr;
    return 0;
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
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
    mic_tcp_ip_addr remote_addr;


    while(IP_recv(&pdu_received, &local_addr, &remote_addr, 100)==-1 || pdu_received.header.dest_port != mon_socket.local_addr.port ||pdu_received.header.ack == 0 || pdu_received.header.ack_num == PE){
        
        if (is_loss_allowed()){
            printf("Perte autorisee\n");
            push_value_window(1); //add a loss
            return size_sent_data;
        }
        printf("Perte refusee\n");
        // printf("Cond 2: %d Cond3: %d Cond 4: %d\n",pdu_received.header.dest_port != mon_socket.local_addr.port,pdu_received.header.ack == 0,pdu_received.header.ack_num != PE);
        // printf("source port: %d dest port: %d ack: %d\n",pdu.header.source_port, pdu.header.dest_port, pdu.header.ack);
        size_sent_data = IP_send(pdu, mon_socket.remote_addr.ip_addr);
    }
    push_value_window(0); //add a success
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
    if (pdu.header.dest_port == mon_socket.local_addr.port){
        if (pdu.header.ack == 0 || (pdu.header.ack == 1 && pdu.header.syn == 1)){
            if (pdu.header.seq_num == PA){
                app_buffer_put(pdu.payload);
                PA = (PA +1)%2;
            }
            mic_tcp_pdu pdu_ack;
            pdu_ack.header.source_port = mon_socket.local_addr.port;
            pdu_ack.header.dest_port = pdu.header.source_port;
            pdu_ack.header.ack_num = PA;
            pdu_ack.header.ack = 1;
            // printf("source port: %d dest port: %d ack_num: %d ack: %d\n",pdu_ack.header.source_port, pdu_ack.header.dest_port, pdu_ack.header.ack_num, pdu_ack.header.ack);
            IP_send(pdu_ack, remote_addr); //a voir si c'est pas local addr
        }
    }
}
