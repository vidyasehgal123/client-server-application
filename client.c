#define MAX_BUFFER_SIZE 1024
#define MAX_COMMAND_LENGTH 1000
#include <utime.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/fcntl.h>
#include <time.h>
typedef struct File File;
//Unzip File Flag. 0 - unzip, 1 - no unzip
int isUnzip = 0;
//Converting this string date to date format %Y-%m-%d
time_t convertStringToDate(char *date) {
    struct tm tm = {0};
    if (strptime(date, "%Y-%m-%d", &tm) == NULL) {
        return -1;
    }
    tm.tm_isdst = -1;
    time_t temp;
    temp = mktime(&tm);
    return temp;
}
//This function takes a raw command as input and validates it based on the command type. It returns the command type if it is valid and returns -1 if it is invalid. 
// 1 - findFile 2 - sgetfiles 3 - dgetfiles // 4 - getfiles // 5 - gettargz // 6 - exit
int validatecommandreturntype(char *rawCommand) {
    char *ptr = strtok(rawCommand, " ");
    char *local[50];
    int cnt = 0;
    int i = 0;
    while (1) {
        char *ptr1 = strtok(NULL, " ");
        if (ptr1 == NULL) {
            break;
        }
        if (strcmp(ptr1, "\n") == 0) {
            continue;
        }
        local[i] = ptr1;
        i++;
        cnt++;
    }
    if (cnt > 0 && strcmp(local[cnt - 1], "-u\n") == 0) {
        isUnzip = 1;
    }
    if (strcmp(ptr, "findfile") == 0) {
        if (cnt != 1) {
            fprintf(stderr, "Command Invalid - findfile `filename`\n");
            return -1;
        }
        return 1;
    }

    if (strcmp(ptr, "sgetfiles") == 0) {
        if (cnt < 2 || cnt > 3) {
            fprintf(stderr, "Command Invalid - sgetfiles size1 size2 <-u>\n");
            return -1;
        }

        int size1 = atoi(local[0]);
        int size2 = atoi(local[1]);
        if (size1 < 0 || size2 < 0) {
            fprintf(stderr, "Command Invalid - sgetfiles size1 size2 <-u>: [Size1, Size2] >= 0\n");
            return -1;
        }

        if (size2 < size1) {
            fprintf(stderr,
                    "Command Invalid - sgetfiles size1 size2 <-u>: Size 1 should be less than equal to size 2\n");
            return -1;
        }

        return 2;
    }

    if (strcmp(ptr, "dgetfiles") == 0) {
        if (cnt < 2 || cnt > 3) {
            fprintf(stderr, "Command Invalid - dgetfiles size1 size2 <-u>\n");
            return -1;
        }

        time_t date1, date2;
        date1 = convertStringToDate(local[0]);
        date2 = convertStringToDate(local[1]);
        if (date1 == -1 || date2 == -1) {
            fprintf(stderr, "Invalid date format should YYYY-MM-DD\n");
            return -1;
        }

        if (date2 < date1) {
            fprintf(stderr, "date2 should be greater than equal to date1\n");
            return -1;
        }
        return 2;
    }
   if (strcmp(ptr, "getfiles") == 0) {

        if (isUnzip == 0 && cnt > 6) {
            fprintf(stderr,
                    "Command Invalid - getfiles file1 file2 file3 file4 file5 file6(file 1 ..up to file6) <-u>\n");
            return -1;
        }

        if (cnt < 1 || cnt > 7) {
            fprintf(stderr,
                    "Command Invalid - getfiles file1 file2 file3 file4 file5 file6(file 1 ..up to file6) <-u>\n");
            return -1;
        }

        return 4;
    }

    if (strcmp(ptr, "gettargz") == 0) {
        if (isUnzip == 0 && cnt > 6) {
            fprintf(stderr, "Command Invalid - gettargz <extension list> <-u> //up to 6 different file types\n");
            return -1;
        }
        if (cnt < 1 || cnt > 7) {
            fprintf(stderr, "Command Invalid - gettargz <extension list> <-u> //up to 6 different file types\n");
            return -1;
        }
        return 5;
    }

    if (strcmp(ptr, "quit\n") == 0) {
        if (cnt) {
            fprintf(stderr, "Command Invalid - quit\n");
            return -1;
        }
        return 6;
    }
 fprintf(stderr, "Command not supported!\n");
    return -1;
}

//This function takes a filename as input and unzips it using the tar command.
int unzippingfile(char *fileName) {
    // Run the tar command to extract the files
    printf("unzipping file %s\n", fileName);
    char buffer[100];
    int n = sprintf(buffer, "tar -xzvf %s", fileName);
    buffer[n] = '\0';
    int ret = system(buffer);
    if (ret != 0) {
        printf("Error extracting files\n");
        return -1;
    }
    printf("file %s unzipped\n", fileName);
    return 1;
}

//Receive the tar file from the server
void handleReceivingFileFromServer(int sockfd, int doUnzip) {
    int pid = fork();
    if (pid == 0) {
        char buffer[MAX_COMMAND_LENGTH];
        char tarName[40];
        sprintf(tarName, "temp.tar.gz");

        // Open file
        int fd = open(tarName, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
        if (fd < 0) {
            perror("open");
            exit(EXIT_FAILURE);
        }

        // Receive file contents
        ssize_t bytes_received = 1;
        long int total_bytes_received = 0;
        while (bytes_received > 0) {
            bytes_received = recv(sockfd, buffer, MAX_BUFFER_SIZE, 0);
            if (bytes_received < 0) {
                perror("recv");
                exit(EXIT_FAILURE);
            }

            long int bytes_written = write(fd, buffer, bytes_received);
            if (bytes_written < 0) {
                perror("write");
                exit(EXIT_FAILURE);
            }

            total_bytes_received += bytes_written;
        }

        printf("File received successfully.\n");
        printf("Total file received %d\n", total_bytes_received);
        // Clean up
        close(fd);
        exit(0);
    } else {
        sleep(10);
        kill(pid, SIGKILL);
        printf("parent received success\n");
        if (doUnzip == 1) {
            unzippingfile("temp.tar.gz");
        }
    }
}

//Connect to server using sockets
int connectToServer(char *host, char *port) {
    int server, portNumber;
    struct sockaddr_in servAdd;
    if ((server = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Cannot create socket\n");
        exit(1);
    }
    servAdd.sin_family = AF_INET;
    sscanf(port, "%d", &portNumber);
    servAdd.sin_port = htons((uint16_t) portNumber);

    if (inet_pton(AF_INET, host, &servAdd.sin_addr) < 0) {
        fprintf(stderr, " inet_pton() has failed\n");
        exit(2);
    }
    if (connect(server, (struct sockaddr *) &servAdd, sizeof(servAdd)) < 0) {
        fprintf(stderr, "connection to server failed, Exiting...\n");
        exit(3);
    }

    return server;
}

int main(int argc, char *argv[]) {
    char message[255];
    if (argc != 3) {
        fprintf(stderr, "Invalid Client start. <ClientFilePath> <Host> <Port>\n");
        exit(0);
    }
    int server;
    server = connectToServer(argv[1], argv[2]);
    long int n1;
    printf("reading...\n");
    if ((n1 = read(server, message, 255)) < 0) {
        fprintf(stderr, "Failed reading message from server\n");
        exit(3);
    }
    message[n1] = '\0';

    if (strcmp(message, "success") != 0) {
        close(server);
        server = connectToServer(message, argv[2]);
    }

    printf("Connected to server!!\n");
    while (1) {
        int n;
        isUnzip = 0;
        printf("ms$ ");
        char rawCommand[1000];
        fgets(rawCommand, MAX_COMMAND_LENGTH, stdin);
        char *copy = malloc(strlen(rawCommand) + 1);
        strcpy(copy, rawCommand);

        //Raw Command
        rawCommand[strcspn(rawCommand, "\n")] = '\0';

        //Validation of command input by the user
        int type = validatecommandreturntype(copy);
        if (type == -1) {
            continue;
        }

        //Forward command to server
        write(server, rawCommand, strlen(rawCommand) + 1);

        if (!strcasecmp(rawCommand, "Bye\n")) {
            kill(getppid(), SIGTERM);
            exit(0);
        }
        printf("Request Sent, waiting for server response\n");

        //Server Response
        if ((n = read(server, message, 255)) < 0) {
            fprintf(stderr, "Failed reading message from server\n");
            exit(3);
        }

        message[n] = '\0';
        printf("message from server: %s\n", message);

        if (strcasecmp(message, "sending file\n") == 0) {
            //Receive file from server
            handleReceivingFileFromServer(server, isUnzip);
        }

        if (strcmp(message, "Exit") == 0) {
            fprintf(stderr, "I(Client) am Exiting...");
            close(server);
            exit(0);
        }
    }
}