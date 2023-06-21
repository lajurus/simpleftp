#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <err.h>

#include <netinet/in.h>
#include <errno.h>
#include <sys/stat.h>

#define BACKLOG 20

#define BUFSIZE 512
#define CMDSIZE 4
#define MSGSIZE 128
#define PARSIZE 100

#define MSG_220 "220 srvFtp version 1.0\r\n"
#define MSG_331 "331 Password required for %s\r\n"
#define MSG_230 "230 User %s logged in\r\n"
#define MSG_530 "530 Login incorrect\r\n"
#define MSG_221 "221 Goodbye\r\n"
#define MSG_550 "550 %s: no such file or directory\r\n"
#define MSG_299 "299 File %s size %ld bytes\r\n"
#define MSG_226 "226 Transfer complete\r\n"

/**
 * function: receive the commands from the client
 * sd: socket descriptor
 * operation: \0 if you want to know the operation received
 *            OP if you want to check an especific operation
 *            ex: recv_cmd(sd, "USER", param)
 * param: parameters for the operation involve
 * return: only usefull if you want to check an operation
 *         ex: for login you need the seq USER PASS
 *             you can check if you receive first USER
 *             and then check if you receive PASS
 **/
bool recv_cmd(int sd, char *operation, char *param) {
    char buffer[BUFSIZE], *token;
    int recv_s;

    // receive the command in the buffer and check for errors
    recv_s = recv(sd, buffer, BUFSIZE,0);

    // expunge the terminator characters from the buffer
    buffer[strcspn(buffer, "\r\n")] = 0;

    // complex parsing of the buffer
    // extract command receive in operation if not set \0
    // extract parameters of the operation in param if it needed
    token = strtok(buffer, " ");
    if (token == NULL || strlen(token) < 4) {
        warn("not valid ftp command");
        return false;
    } else {
        if (operation[0] == '\0') strcpy(operation, token);
        if (strcmp(operation, token)) {
            warn("abnormal client flow: did not send %s command", operation);
            return false;
        }
        token = strtok(NULL, " ");
        if (token != NULL) strcpy(param, token);
    }
    return true;
}

/**
 * function: send answer to the client
 * sd: file descriptor
 * message: formatting string in printf format
 * ...: variable arguments for economics of formats
 * return: true if not problem arise or else
 * notes: the MSG_x have preformated for these use
 **/
bool send_ans(int sd, char *message, ...){
    char buffer[BUFSIZE];

    int len, bytes_sent;

    va_list args;
    va_start(args, message);

    vsprintf(buffer, message, args);
    va_end(args);

    len = strlen(buffer);

    // send answer preformated and check errors
    bytes_sent = send(sd, buffer, len, 0);
    if(bytes_sent==-1) {
        printf("Error: send! %s\n", strerror(errno));
        return false;
    }

    return true;
}

long int findSize(char *filename)
{
    struct stat file_status;
    if (stat(filename, &file_status) < 0) {
        return -1;
    }

    return file_status.st_size;
}

/**
 * function: RETR operation
 * sd: socket descriptor
 * file_path: name of the RETR file
 **/

void retr(int sd, char *file_path) {
    FILE *file;    
    int bread;
    long int fsize;
    char buffer[BUFSIZE];
    char msg[MSGSIZE];

    printf("Enviando archivo\n");
    
    fsize = findSize(file_path);

    // check if file exists if not inform error to client
    file = fopen(file_path, "r");
    if (file == NULL){
        sprintf(msg, MSG_550, file_path);
        if (!(send_ans(sd, msg))){
            exit(1);
        }
    }
    
    // send a success message with the file length
    sprintf(msg, MSG_299, file_path, fsize);
    if (!(send_ans(sd, msg))){
        exit(1);
    }



    // send the file
    while(!feof(file)) {
        bread = fread(buffer, 1, BUFSIZE, file);

        if(bread > 0) {
            send(sd, buffer, bread, 0);
            // important delay to avoid problems with buffer size
            sleep(1);
        }
    }

    // close the file
    fclose(file);


    printf("archivo envado\n");

    // send a completed transfer message
    send_ans(sd, MSG_226);

}
/**
 * funcion: check valid credentials in ftpusers file
 * user: login user name
 * pass: user password
 * return: true if found or false if not
 **/
bool check_credentials(char *user, char *pass) {
    FILE *file;
    char *path = "./ftpusers", *line = NULL, credentials[100];
    size_t line_size = 0;
    bool found = false;

    // make the credential string
    sprintf(credentials, "%s:%s", user, pass);

    // check if ftpusers file it's present
    if ((file = fopen(path, "r"))==NULL) {
        warn("Error opening %s", path);
        return false;
    }

    // search for credential string
    while (getline(&line, &line_size, file) != -1) {
        strtok(line, "\n");
        if (strcmp(line, credentials) == 0) {
            found = true;
            break;
        }
    }

    // close file and release any pointers if necessary
    fclose(file);
    if (line) free(line);

    // return search status
    return found;
}

/**
 * function: login process management
 * sd: socket descriptor
 * return: true if login is succesfully, false if not
 **/
bool authenticate(int sd) {
    char user[PARSIZE], pass[PARSIZE];
    char msg[MSGSIZE];

    // wait to receive USER action
    if (!recv_cmd(sd,"USER",user)){
        return false;
    }

    // ask for password
    sprintf(msg, MSG_331, user);
    if (!(send_ans(sd, msg))){
        return false;
    }

    // wait to receive PASS action
    if (!recv_cmd(sd,"PASS",pass)){
        return false;
    }

    // if credentials don't check denied login
    if (!check_credentials(user,pass)){
        send_ans(sd, MSG_530);
        return false;
    } else {
        // confirm login
        sprintf(msg,MSG_230, user);
        if (!(send_ans(sd, msg))){
            return false;
        }
        return true;
    }


}

/**
 *  function: execute all commands (RETR|QUIT)
 *  sd: socket descriptor
 **/
void operate(int sd) {
    char op[CMDSIZE+1], param[PARSIZE];

    while (true) {
        op[0] = param[0] = '\0';
        // check for commands send by the client if not inform and exit
        recv_cmd(sd, op, param);
        //strncpy(op, op, CMDSIZE);
        //printf("b. op %s\n", op);
        if (strcmp(op, "RETR") == 0) {
            retr(sd, param);
        } else if (strcmp(op, "QUIT") == 0) {
            // send goodbye and close connection
            send_ans(sd, MSG_221);
            close(sd);
            printf("Conexion cerrada... \n");
            break;
        } else {
            printf("Comando invalido... \n");
            // invalid command
            // furute use
        }
    }
}

/**
 * Run with
 *         ./mysrv <SERVER_PORT>
 **/
int main (int argc, char *argv[]) {
    setbuf(stdout, NULL);
    // arguments checking
    if (argc < 2) {
        errx(1, "Port expected as argument");
    } else if (argc > 2) {
        errx(1, "Too many arguments");
    }

    // reserve sockets and variables space
    int master_sd, slave_sd;
    struct sockaddr_in master_addr, slave_addr;

    // create server socket and check errors
    /*
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // usar IPv4 o IPv6, cualquiera
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // completa mi IP por mi

    getaddrinfo(NULL, argv[1], &hints, &res);
    */
    if((master_sd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Error: could not create new socket %s\n", strerror(errno));
        return 1;
    }

    master_addr.sin_family = AF_INET;
    master_addr.sin_port = htons(atoi(argv[1])); // short, orden de bytes de red
    master_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    memset(master_addr.sin_zero, '\0', sizeof master_addr.sin_zero);
    
    // bind master socket and check errors
    if(bind(master_sd, (struct sockaddr *)&master_addr, sizeof master_addr) < 0) {
        printf("\n Error: bind %s\n", strerror(errno));
        return 1;
    }

    // make it listen
    if(listen(master_sd, BACKLOG) < 0){
        printf("\n Error: listen %s\n", strerror(errno));
        return 1;
    }

    socklen_t addr_size;
    
    addr_size = sizeof (master_addr);
    // main loop
    while (true) {
        // accept connectiones sequentially and check errors
        slave_sd = accept(master_sd, (struct sockaddr *)&master_addr, &addr_size);

        // send hello
        if (!(send_ans(slave_sd, MSG_220))){
            return 1;
        }

        // operate only if authenticate is true
        if (authenticate(slave_sd)){
            operate(slave_sd);
        } else {
            printf("Intento de login incorrecto...\n");
        }
        
    }

    // close server socket
    close(master_sd);

    return 0;
}
