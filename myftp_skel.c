#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <err.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#define BUFSIZE 512

/**
 * function: receive and analize the answer from the server
 * sd: socket descriptor
 * code: three leter numerical code to check if received
 * text: normally NULL but if a pointer if received as parameter
 *       then a copy of the optional message from the response
 *       is copied
 * return: result of code checking
 **/
bool recv_msg(int sd, int code, char *text) {
    char buffer[BUFSIZE], message[BUFSIZE];
    int recv_s, recv_code;

    // receive the answer
    recv_s = recv(sd, buffer, BUFSIZE, 0);

    // error checking
    if (recv_s < 0) warn("error receiving data");
    if (recv_s == 0) errx(1, "connection closed by host");

    // parsing the code and message receive from the answer
    sscanf(buffer, "%d %[^\r\n]\r\n", &recv_code, message);
    printf("%d %s\n", recv_code, message);
    // optional copy of parameters
    if(text) strcpy(text, message);
    // boolean test for the code
    return (code == recv_code) ? true : false;
}

/**
 * function: send command formated to the server
 * sd: socket descriptor
 * operation: four letters command
 * param: command parameters
 **/
void send_msg(int sd, char *operation, char *param) {
    char buffer[BUFSIZE] = "";
    int len, bytes_sent;
    
    // command formating
    if (param != NULL)
        sprintf(buffer, "%s %s\r\n", operation, param);
    else
        sprintf(buffer, "%s\r\n", operation);

    len = strlen(buffer);

    printf("Enviando: %s",buffer);
    // send command and check for errors
    bytes_sent = send(sd, buffer, len, 0);
    if(bytes_sent==-1) {
        printf("Error: send! %s\n", strerror(errno));
    }
}

/**
 * function: simple input from keyboard
 * return: input without ENTER key
 **/
char * read_input() {
    char *input = malloc(BUFSIZE);
    if (fgets(input, BUFSIZE, stdin)) {
        return strtok(input, "\n");
    }
    return NULL;
}

/**
 * function: login process from the client side
 * sd: socket descriptor
 **/
void authenticate(int sd) {
    char *input, desc[100];
    int code;

    // ask for user
    printf("username: ");
    input = read_input();

    // send the command to the server
    send_msg(sd,"USER",input);

    // relese memory
    free(input);

    // wait to receive password requirement and check for errors
    if(!(recv_msg(sd,331,NULL))){
        exit(1);
    }

    // ask for password
    printf("passwd: ");
    input = read_input();

    // send the command to the server
    send_msg(sd,"PASS",input);

    // release memory
    free(input);

    // wait for answer and process it and check for errors
    if (recv_msg(sd,230,NULL)){
        printf("Login correcto\n");
    } else{
        printf("Login incorrecto\n");
        exit(1);
    }

}

/**
 * function: operation get
 * sd: socket descriptor
 * file_name: file name to get from the server
 **/
void get(int sd, char *file_name) {
    char desc[BUFSIZE], buffer[BUFSIZE];
    int f_size, recv_s, r_size = BUFSIZE;
    FILE *file;

    // send the RETR command to the server
    send_msg(sd, "RETR", file_name);

    // check for the response
    if (recv_msg(sd,299,NULL)){
        printf("Descargando archivo\n");
    } else{
        printf("Error al realizar el get\n");
        exit(1);
    }

    // parsing the file size from the answer received
    // "File %s size %ld bytes"
    sscanf(buffer, "File %*s size %d bytes", &f_size);

    // open the file to write
    file = fopen(file_name, "w");

    //receive the file
    while(1) {
        recv_s = recv(sd, desc, r_size, 0);
        if(recv_s > 0) {
            fwrite(desc, 1, recv_s, file);
        }
        if(recv_s < r_size) {
            break;
        }
    }

    // close the file
    fclose(file);
    
    printf("Archivo creado\n");

    // receive the OK from the server
    if(!(recv_msg(sd,226,NULL))){
        exit(1);
    }
}

/**
 * function: operation quit
 * sd: socket descriptor
 **/
void quit(int sd) {
    // send command QUIT to the client
    send_msg(sd, "QUIT",NULL);
    // receive the answer from the server
    recv_msg(sd,221,NULL);
}

/**
 * function: make all operations (get|quit)
 * sd: socket descriptor
 **/
void operate(int sd) {
    char *input, *op, *param;

    while (true) {
        printf("Operation: ");
        input = read_input();
        if (input == NULL)
            continue; // avoid empty input
        op = strtok(input, " ");
        // free(input);
        if (strcmp(op, "get") == 0) {
            param = strtok(NULL, " ");
            get(sd, param);
        }
        else if (strcmp(op, "quit") == 0) {
            quit(sd);
            break;
        }
        else {
            // new operations in the future
            printf("TODO: unexpected command\n");
        }
        free(input);
    }
    free(input);
}

/**
 * Run with
 *         ./myftp <SERVER_IP> <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {
    setbuf(stdout, NULL);
    printf("init....\n");
    int sd;
    struct sockaddr_in addr;
    
    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo;  

    // arguments checking
    if (argc < 3) {
        errx(1, "IP address and Port expected as argument");
    } else if (argc > 3) {
        errx(1, "Too many arguments");
    }

    // create socket and check for errors
    if((sd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Error: could not create new socket %s\n", strerror(errno));
        return 1;
    }

    // set socket data    
    memset(&hints, 0, sizeof hints); // asegúrate que la estructura está vacía
    hints.ai_family = AF_UNSPEC; // no importa si es IPv4 o IPv6
    hints.ai_socktype = SOCK_STREAM; // sockets de stream TCP
    hints.ai_flags = AI_PASSIVE; // completa mi IP por mi
    if ((status = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }

    // connect and check for errors
    if(connect(sd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
        printf("\n Error: connect. %s\n", strerror(errno));
        return 1;
    }

    char buffer[BUFSIZE];
    // if receive hello proceed with authenticate and operate if not warning
    if(recv_msg(sd,220,NULL)){
        printf("\n Conexion establecida.\n");
    } else{
        printf("\n Error al establecer conexion.\n");
        return 1;
    }
    authenticate(sd);
    operate(sd);
    
    // close socket
    close(sd);
    freeaddrinfo(servinfo);

    return 0;
}
