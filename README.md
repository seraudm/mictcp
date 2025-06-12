# BE RESEAU
## TPs BE Reseau - 3 MIC: Maël Seraud et Thomas Verdeil

## Compilation du protocole mictcp et lancement des applications de test fournies

Pour compiler mictcp et générer les exécutables des applications de test taper :

    make

Deux applicatoins de test sont fournies, tsock_texte et tsock_video, elles peuvent être lancées soit en mode puits, soit en mode source selon la syntaxe suivante:

    Usage: ./tsock_texte [-p|-s destination] port
    Usage: ./tsock_video [[-p|-s] [-t (tcp|mictcp)]

Seul tsock_video permet d'utiliser, au choix, votre protocole mictcp ou une émulation du comportement de tcp sur un réseau avec pertes.

## Ce qui marche, ce qui ne marche pas

Tout fonctionne bien.

## Choix d'implémentation

### Stop and wait en fiabilité partielle

Pour mettre en place notre fiabilité partielle, nous avons fait le choix d'utiliser une fenêtre glissante mise en place à l'aide d'un buffer circulaire que nous utilisons pour calculer un taux de perte en comptant le nombre de pertes sur le nombre d'envoi. L'interêt d'utiliser une fenêtre glissante au lieu de simplement faire un ratio sur la totalité des envois permet d'éviter que lorsque les erreurs arrivent de façon consécutive (ce qui est souvent le cas en pratique), elles soient toutes acceptées car il n'y a pas eu d'erreur au tout début de la commmunication.

Dans le cas précis de la vidéo, ne pas utiliser de fenêtre glissante aurait pour conséquence de d'avoir des grands sauts dans la vidéo quand les erreurs arrivent à la chaîne ce qui est problématique pour l'utilisateur. 

Nous avons fixé la taille de cette fenêtre glissante à 100 pour que lorsque l'on choisit le pourcentage de perte (valeur de 0 à 100), le fait de changer d'un pourcent change réellement le comportement de notre protocole(si on avait pris une taille de 5 par exemple, de 0 à 20% de perte le comportement serait le même).

### Négociation de pertes

Nous avons fait le choix que le client et le serveur propose chacun un taux de perte et c'est le plus contraignant des 2 qui est accepté (celui dont la valeur est le plus faible). Cela nous semblait logique car en faisant de cette manière, les contraintes sont respectées des 2 côtés quoi qu'il arrive. 

Pour déterminer lequel des deux est le plus contraignat, c'est le serveur au moment de recevoir le syn qui compare les taux de perte et renvoie dans le synack celui qui est choisi pour que le client ait connaissance de sa valeur.

### Asynchronisme

Pour mettre en place l'asynchronisme entre le thread applicatif et le thread réceptif côté serveur, nous avons fait le choix d'utiliser une variable condition et de s'appuyer sur le champ *state* de la stucture socket.

Le thread applicatif reste donc bloqué dans le accept tant qu'aucune connexion n'a été établie grâce au pthread_cond_wait. C'est le thread réceptif qui le réveille avec le pthread_cond_broadcast et qui est chargé d'envoyé le pdu SYNACK à la récecption d'un SYN.

## Bénéfices MICTCP-v4 par rapport à TCP & MICTCP-v2

MICTCP-v4 par rapport à TCP & MICTCP-v2 met en place un mécanisme de fiabilité partielle au lieu d'une fiabilité totale. C'est particulièrement intéressant pour le transfert de vidéo car la perte d'image de manière ponctuelle n'affecte que très peu le rendu et donc en évitant de toutes les rattraper, on gagne en fluidité.
