#include <utime.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/tcp.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <ftw.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/fcntl.h>

#define SPARE_FDS 5
#define BUFSIZE 4096
#define ALLOWED_FILES_COUNT_LIMIT 5

char *filePath;
char *fileName;
int size1, size2;
time_t date1, date2;
int childDescript;
char *homeDir;
int limit;
char *tempCommand[10] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

//Convert string date to date format %Y-%m-%d
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

//Breaks Raw Command into individual Command
//eg: ls -1 | cat output.txt to ls -1, cat output.txt
void breakRawCommandToIndividualCommand(char *output[50], char *rawCommand) {
    int len = 0;
    char *ptr = strtok(rawCommand, " ");
    while (1) {
        if (ptr == NULL) {
            output[len] = NULL;
            len++;
            break;
        }
        if (strcmp(ptr, "\n") == 0) {
            continue;
        }
        output[len] = ptr;
        len++;
        ptr = strtok(NULL, " ");
    }

    if (len < 1 || len > 9) {
        printf("output arguments should be greater than equal to 1 and less than equal to 8\n");
        exit(10);
    }
}

// Find File fstw callback
int findFileFstw(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if (typeflag == FTW_F) {
        if (strcmp(basename(fpath), fileName) == 0) {
            filePath= fpath;
            return 1;
        }
    }
    return 0;
}

// Convert epoch time to format %Y-%m-%d %H:%M:%S
void convertEpochTimeToDateTime(time_t epoch_time, char *time_str) {
    struct tm *info;
    info = localtime(&epoch_time);

    strftime(time_str, 80, "%Y-%m-%d %H:%M:%S", info);
}

//handle find file Command
void handleFindFileCommand(char *command[10], char *result) {
    int nfds = getdtablesize() - SPARE_FDS;
    fileName = command[1];
    printf("handling command\n");
    //Skim through directory tree rooted at homeDir
    int success = nftw(homeDir, findFileFstw, nfds, FTW_PHYS);
    printf("still on it\n");
    if (success != 1) {
        strcpy(result, "File Not Found");
        return;
    }
    char time_str[80];
    struct stat st;
    char buffer[150];
    stat(filePath, &st);
    convertEpochTimeToDateTime(st.st_birthtimespec.tv_sec, time_str);
    int n = sprintf(buffer, "File Found: fileName - %s, size - %lld, Created At - %s\n", filePath, st.st_size,
                    time_str);
    buffer[n] = '\0';
    strcpy(result, buffer);
}

//Copy file to temperary directory
int doCopyFile(const char *fpath, char *desti) {

    int fd1, fd2;
    char buffer[100];
    long int n1;

    strcat(desti, basename(fpath));
    if ((fd1 = open(fpath, O_RDONLY)) == -1) {
        perror("Failed opening file");
        return 1;
    } else {
        if ((fd2 = open(desti, O_CREAT | O_WRONLY | O_TRUNC, 0700)) == -1) {
            perror("Failed opening file");
            return 1;
        }
    }

    while ((n1 = read(fd1, buffer, 100)) > 0) {
        if (write(fd2, buffer, n1) != n1) {
            perror("writing problem ");
            return 1;
        }
        if (n1 == -1) {
            perror("Reading problem ");
            return 1;
        }
    }
    close(fd2);
    return 0;
}

//Handle Sgetfiles command fstw callback
int handleSgetFileCommandfstw(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if (typeflag == FTW_F) {
        struct stat st;
        if (stat(fpath, &st) == 0) {
            if (st.st_size >= size1 && st.st_size <= size2) {
                limit--;
                char dir[42];
                sprintf(dir, "./%d/", childDescript);
                mkdir(dir, 0700);
                doCopyFile(fpath, dir);
                if (limit == 0) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

// Create Tar method
int createTar(char *tarName, char *desti) {
    fprintf(stderr, "Creating tar...\n");
    char command[100];
    sprintf(command, "tar -czvf %s.tar.gz %s", tarName, desti);

    if (system(command) == -1) {
        printf("Error creating tar.gz archive.\n");
        return -1;
    }
    return 1;
}

//Remove directory firName
int removeFileFolder(char *firName) {
    char command[100];
    sprintf(command, "rm -rf %s", firName);

    if (system(command) == -1) {
        printf("Error removing file/folder.\n");
        return -1;
    }
    return 1;
}

// Send file to client
int send_file(char *fp, int sockfd) {
    int pid = fork();
    if (pid == 0) {
        int filefd, len;
        fprintf(stderr, "Copying file to client %d\n", sockfd);
        filefd = open(fp, O_RDONLY);
        char buffer[BUFSIZE];

        while ((len = read(filefd, buffer, BUFSIZE)) > 0) {
            if (send(sockfd, buffer, len, 0) == -1) {
                perror("send");
                return -1;
            }
        }
        sleep(1);
        fprintf(stderr, "File to client %d sent success\n", childDescript);
        close(filefd);
        close(sockfd);
        exit(0);
    }

    wait(NULL);
    return 1;
}

//handle Sending File To Client
void handleSendingFilesToServer(char *result) {
    char buffer[150];
    char dir[42];
    sprintf(dir, "./%d", childDescript);
    char tarName[40];
    int n = sprintf(tarName, "temp-%d", childDescript);
    tarName[n] = '\0';
    if (createTar(tarName, dir) == -1) {
        n = sprintf(buffer, "Oops something wrong\n");
        buffer[n] = '\0';
        strcpy(result, buffer);
        fprintf(stderr, "Tar creation failed for client %d\n", childDescript);
        return;
    }
    sprintf(dir, "%s.tar.gz", tarName);
    n = sprintf(buffer, "sending file\n");
    buffer[n] = '\0';
    write(childDescript, buffer, strlen(buffer) + 1);

    if (send_file(dir, childDescript) == -1) {
        int n = sprintf(buffer, "Oops something wrong\n");
        buffer[n] = '\0';
        strcpy(result, buffer);
        fprintf(stderr, "Failed sending file to client %d\n", childDescript);
        return;
    }

    removeFileFolder(dir);
    sprintf(dir, "./%d", childDescript);
    removeFileFolder(dir);
    n = sprintf(buffer, "Request for client %d processed successfully\n", childDescript);
    buffer[n] = '\0';
    strcpy(result, buffer);
}

//Handle Sget File Command
void handleSgetFileCommand(char *command[10], char *result) {
    limit = ALLOWED_FILES_COUNT_LIMIT;
    size1 = atoi(command[1]);
    size2 = atoi(command[2]);

    int nfds = getdtablesize() - SPARE_FDS;
    nftw(homeDir, handleSgetFileCommandfstw, nfds, FTW_PHYS);
    if (limit == ALLOWED_FILES_COUNT_LIMIT) {
        strcpy(result, "File Not Found");
        return;
    }
    handleSendingFilesToServer(result);
}

//Handle Dget File Command FSTW Callback
int handleDgetFileCommandfstw(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if (typeflag == FTW_F) {
        struct stat st;
        if (stat(fpath, &st) == 0) {
            time_t creation_time = st.st_birthtimespec.tv_sec;
            printf("%s %ld\n", fpath, creation_time);
            if (creation_time >= date1 && creation_time < (date2+86400)) {
                limit--;
                char dir[42];
                sprintf(dir, "./%d/", childDescript);
                mkdir(dir, 0700);
                doCopyFile(fpath, dir);
                if (limit == 0) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

//Handle Dget Files Command
void handleDgetFileCommand(char *command[10], char *result) {
    limit = ALLOWED_FILES_COUNT_LIMIT;
    int nfds = getdtablesize() - SPARE_FDS;
    date1 = convertStringToDate(command[1]);
    date2 = convertStringToDate(command[2]);

    printf("finding files between epoch time: %ld and %ld\n", date1, date2);
    nftw(homeDir, handleDgetFileCommandfstw, nfds, FTW_PHYS);
    if (limit == ALLOWED_FILES_COUNT_LIMIT) {
        strcpy(result, "File Not Found");
        return;
    }
    handleSendingFilesToServer(result);
}

// Handle Get Files Command FSTW Callback
int handleGetFilesCommandfstw(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if (typeflag == FTW_F) {
        int i = 0;
        while (tempCommand[i] != NULL) {
            if (strcmp(basename(fpath), tempCommand[i]) == 0) {
                limit--;
                char dir[42];
                sprintf(dir, "./%d/", childDescript);
                mkdir(dir, 0700);
                doCopyFile(fpath, dir);
                if (limit == 0) {
                    return 1;
                }
            }
            i++;
        }
    }
    return 0;
}

//Handle Get Files Command
void handleGetFilesCommand(char *command[10], char *result) {
    limit = ALLOWED_FILES_COUNT_LIMIT;
    int nfds = getdtablesize() - SPARE_FDS;
    int i = 0;
    while (command[i] != NULL) {
        tempCommand[i] = command[i];
        i++;
    }
    tempCommand[i] = NULL;
    nftw(homeDir, handleGetFilesCommandfstw, nfds, FTW_PHYS);
    if (limit == ALLOWED_FILES_COUNT_LIMIT) {
        strcpy(result, "File Not Found");
        return;
    }
    handleSendingFilesToServer(result);
}

// Handle GetArgzCommand FSTW Callback
int handleGetArgzCommandfstw(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if (typeflag == FTW_F) {
        int i = 0;
        char *extension = strrchr(fpath, '.');
        while (tempCommand[i] != NULL) {
            if (strcmp(extension, tempCommand[i]) == 0) {
                limit--;
                char dir[42];
                sprintf(dir, "./%d/", childDescript);
                mkdir(dir, 0700);
                doCopyFile(fpath, dir);
                if (limit == 0) {
                    return 1;
                }
            }
            i++;
        }
    }
    return 0;
}

//Handle Get Argz Command
void handleGetArgzCommand(char *command[10], char *result) {
    limit = ALLOWED_FILES_COUNT_LIMIT + 5;
    int nfds = getdtablesize() - SPARE_FDS;
    int i = 0;
    while (command[i] != NULL) {
        tempCommand[i] = command[i];
        i++;
    }
    tempCommand[i] = NULL;
    nftw(homeDir, handleGetArgzCommandfstw, nfds, FTW_PHYS);
    if (limit == ALLOWED_FILES_COUNT_LIMIT) {
        strcpy(result, "File Not Found");
        return;
    }
    handleSendingFilesToServer(result);
}

//Handle All the commands
void handleCommand(char *message, char *result) {
    char *command[10];
    breakRawCommandToIndividualCommand(command, message);
    if (strcmp(command[0], "findfile") == 0) {
        handleFindFileCommand(command, result);
        return;
    }

    if (strcmp(command[0], "sgetfiles") == 0) {
        handleSgetFileCommand(command, result);
        return;
    }

    if (strcmp(command[0], "dgetfiles") == 0) {
        handleDgetFileCommand(command, result);
        return;
    }

    if (strcmp(command[0], "getfiles") == 0) {
        handleGetFilesCommand(command, result);
        return;
    }

    if (strcmp(command[0], "gettargz") == 0) {
        handleGetArgzCommand(command, result);
        return;
    }

    if (strcmp(command[0], "quit") == 0) {
        strcpy(result, "Exit");
        return;
    }
    strcpy(result, "Bad Request!, Command not supported by the server\n");
}

// Handle Request from client
void requestHandler(int sd) {
    childDescript = sd;
    char message[255];
    while (1) {
        int n = read(sd, message, 255);
        if (n <= 0) {
            fprintf(stderr, "Bye, client %d dead, wait for a new client\n", sd);
            close(sd);
            exit(0);
        }
        message[n] = '\0';
        printf("Command received from client %d : %s, Processing....\n", sd, message);
        char result[255];
        handleCommand(message, result);
        write(sd, result, strlen(result) + 1);
        printf("Command Processed from client %d with message %s \n", sd, result);
    }
}

int main(int argc, char *argv[]) {
    int sd, clientDescriptor, portNumber;
    struct sockaddr_in servAdd;

    if (argc != 2) {
        fprintf(stderr, "Invalid Server start. <ServerFilePath> <Port>\n");
        exit(0);
    }

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Could not create socket. Exiting...\n");
        exit(1);
    }

    servAdd.sin_family = AF_INET;
    servAdd.sin_addr.s_addr = htonl(INADDR_ANY);
    sscanf(argv[1], "%d", &portNumber);
    servAdd.sin_port = htons((uint16_t) portNumber);
    bind(sd, (struct sockaddr *) &servAdd, sizeof(servAdd));
    listen(sd, 5);
    printf("Server started, listening on port %d\n", portNumber);
    homeDir = strcat(getenv("HOME"), "/CustomHome");

    while (1) {
        printf("Waiting for Client to Accept\n");
        clientDescriptor = accept(sd, (struct sockaddr *) NULL, NULL);
        printf("Received client with id %d\n", clientDescriptor);
        if (!fork())
            requestHandler(clientDescriptor);
        close(clientDescriptor);
    }
}
