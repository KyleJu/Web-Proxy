#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h> 
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>

FILE *filter;
int filter_len = 0;
char *filter_url[100];
#define THREADNUM 4


/*
Check Filter-List
*/

int checkfilterList(char* host) {
	for(int i = 0; i < filter_len; i++){
		// iterate through the string to check occurrence of filter url
		if (strstr(host,filter_url[i]) != NULL){
			return -1;
		}
	}
	return 1;
}

/* 
Conver uri to ASCII character
*/
void url_conversion(char * st, char* encod) {
	for(int i = 0; i < 256; i++) {
		char cur = st[i];
		if (!cur) break;
		//check input characters that are not a-z, A-Z, 0-9, '-', '.', '_' or '~' 
		// replace all illegal characters with "_"
		encod[i] = (isalnum(cur)||cur == '~'||cur  == '-'||cur == '.'||cur == '_') ? cur:'_';
	}
}

//Parsing the request
void parsingRequest(char* buffer, int clientsockfd) {
	/*
	Format:
	From: GET http://www.cs.ubc.ca/~acton/lawler.txt HTTP/1.1
	To:   GET /~acton/lawler.txt HTTP/1.1
		  Host: www.cs.ubc.ca
	*/

	int sockfd;
	struct hostent *htn;
	struct sockaddr_in host_addr;
	char request_type[4];
	char filename[1024];
	char host[256];
	char port[6];
	char uri[1024];
	char httpversion[16];
	char data[4096];
	char* strptr;
    //set the default port
	strcpy(port, "80");
	sscanf(buffer, "%s %s %s\n", request_type, uri, httpversion);

	   // Check Request type
	if (strstr(request_type, "GET") == NULL){
    	//Not GET request
		printf("%s 405 Method Not Allowed", httpversion);
		snprintf(data, sizeof data, "%s 405 Method Not Allowed\n", httpversion);
		send(clientsockfd, data, sizeof data, 0);
		goto closing;
	}

	strptr = strtok(uri, "//");
	strcpy(host, strtok(NULL, "/"));
	strcpy(filename, strtok(NULL,":"));

	char * tmm = strtok(NULL, ":");
	//check port num exists 
	if (tmm != NULL) strcpy(port, tmm);

	//convert to ASCII
	char encode[256];
	memset(uri, 0, sizeof uri);
	// Create file name
	snprintf(uri, sizeof uri, "%s/%s", host, filename);

	url_conversion(uri, encode);


   	//check filter-list
	if (checkfilterList(host) < 0) {
		memset(data, 0, sizeof data);
		sprintf(data, "403: BAD REQUEST\nURL IN BLACKLST\n%s\n", host);
		send(clientsockfd, data, sizeof data, 0);
		goto closing;
	}
	
	//check cache directory 
	mkdir("./cache/", 0700);
	char filePath[300];
    // create the file path
	sprintf(filePath, "./cache/%s", encode);
	if (access(filePath, 0) == 0) {
		int ffo;
		int ress;
		if((ffo = open (filePath, O_RDONLY)) < 0) {
			perror("Error on opening cahce file");
			goto closing;
		}
		do {
			bzero((char*)data, 4096);
			ress = read(ffo, data, 4096);
			if(ress > 0) {
                // send it to the browser
				send(clientsockfd, data, ress, 0); 
			}
		} while(ress > 0);

		goto closing;
	}
    // create sending request uri to the host
	snprintf(data, sizeof data, "%s /%s %s\r\nHost: %s\r\nConnection: close\r\n\r\n", request_type, filename, httpversion, host);

	printf("%s", data);

    // check host name validity
	htn = gethostbyname(host);
	if (htn == NULL) {
		fprintf(stderr, "ERROR, no such host\n");
		memset(data, 0, sizeof data);
		sprintf(data, "403: BAD REQUEST\nHost does not exist\n%s\n", host);
		send(clientsockfd, data, sizeof data, 0);
		goto closing;
	}

	host_addr.sin_family = AF_INET;
	bcopy((char *)htn->h_addr, (char *)&host_addr.sin_addr.s_addr, htn->h_length);
	host_addr.sin_port = htons(atoi(port));

    // create socket 
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("ERROR opening sockets for the host");
		goto closing;
	}

    //connect to the host
	if (connect (sockfd, (struct sockaddr*) &host_addr, sizeof(struct sockaddr)) < 0){
		perror("ERROR connecting to the host");
		goto closing;
	}

    //send the request to the host
	int response = send(sockfd, data, strlen(data), 0);
	if (response < 0) {
		perror("Error writing to socket");
		goto closing;
	}

    // while it can receive data from the host
	int fo = -1;
	do{
		memset(data, 0, sizeof data);
		response = recv(sockfd, data, sizeof data, 0);
		// send back the data
		if (response > 0) {
			//printf("%s", data);
			float version;
			int httpResponse;
			//Check httpversion
			sscanf(buffer, "HTTP/%f %d", &version, &httpResponse);
           	// if resposne is 304, go to closing
			if (httpResponse == 304) goto closing;
			send(clientsockfd, data, response, 0);
			// cache the file
			if (fo == -1) {
				int respo = 0;
				if((fo = open(filePath, O_RDWR|O_TRUNC|O_CREAT, S_IRWXU)) < 0){
					perror("Error on opening cache file");
					goto closing;
				}
			}
			write(fo, data, response);
		}
	} while(response > 0);


    //closing all sockets
	closing:
	close(sockfd);
	close(clientsockfd);

}

/* 
new thread for each accept
*/
void *acceptThread(void * sd) {

	char buffer[10240];
	int sockfd = (int) sd;
	struct sockaddr_in cli_addr;
	unsigned int addlen = sizeof cli_addr;
	int newsockfd;

	memset(&cli_addr, 0, sizeof cli_addr);
	//Waiting for request from clients
	newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &addlen);
	if (newsockfd < 0) {
		perror("ERROR on accept");
		exit(1);
	}

	memset(buffer, '\0', sizeof buffer);

	char* tmp = buffer;
	int byteLeft = 10240;
	int nn;
	//Wait for HTTP request
	while((nn = recv(newsockfd, buffer, 4096, 0)) >0) {
		tmp += nn;
		byteLeft -= nn;
	}
	// receive message
	printf("The message is %s\n", buffer);

	parsingRequest(buffer, newsockfd);
}



int main(int argc, char **argv) {
	int sockfd;
	struct sockaddr_in serv_addr;
	char buffer[4096];
	//check argument
	if (argc < 2) {
		printf("Usage: <port> (optional)<filter-list>\n");
		exit(1);
	}

	if (argc == 3) {
		//Parse filter list
		char* fileName = argv[2];
		//open the file
		filter= fopen(fileName, "r");
		// if filter doesn't exist
		if (!filter) {
			printf("Errror on openning the filter-list");
			exit(1);
		}
		while(fgets(buffer, 100, filter) != NULL) {
			strtok(buffer, "\n");
			filter_url[filter_len] = (char*) malloc(100);
			strcpy(filter_url[filter_len++], buffer);
		}
		//close file
		fclose(filter);
	}

	int portNum = atoi(argv[1]);

	// initialized sockets
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("ERROR opening socket");
		exit(1);
	}

	//Initialized socket structure, referenced from Tutorial Point
	//http://www.tutorialspoint.com/unix_sockets/socket_server_example.htm
	memset(&serv_addr,0, sizeof serv_addr);
	
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(portNum);

	// bind the host address using bind()
	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
		perror("ERROR on binding");
	}

	// start listening
	listen(sockfd, 50);
	
	// Accept connection from the client

	pthread_t thread_id[THREADNUM];

	// opening new threads for each request
	for(int i = 0; i < THREADNUM; i++) {
		int rei = pthread_create(&thread_id[i], NULL, acceptThread, (void *) sockfd);
		if (rei) {
			fprintf(stderr, "Error - pthread_creat() return code: %d\n",rei);
			exit(1);
		}
	}

	// Thread join
	for (int i = 0; i < THREADNUM; i++) {
		int re = pthread_join(thread_id[i], NULL);
		if (re) {
			printf("ERROR - pthread_join() return code %d\n", re);
			exit(1);
		}
	}

	//free filter url list that were previous malloced
	for(int i = 0; i <filter_len; i++) {
		free(filter_url[i]);
	}

	// close the socket connections
	close(sockfd);
	return 0;
}
