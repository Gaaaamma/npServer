#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/un.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>

#define PET_SIZE 1000
#define MAX_LENGTH 15000
#define MAX_USERNUMBER 30

using namespace std;

// class User
// we use this class to record the client's ID/Name/IP/Port
class User{
private:
	bool available ;
	int ipcSocketfd ;
	int id ;
	int pid ;
	string name ;
	string ipAddress ;
	int port ;

public:
	User(){
		reset();
	}
	void reset(){	
		available = false;
		ipcSocketfd = -1;
		id = -1;
		pid = -1;
		name = "(no name)";
		ipAddress = "";
		port = -1;
	}

	bool getAvailable(){
		return available ;
	}
	void setAvailable(bool available){
		this -> available = available;
	}

	int getIpcSocketfd(){
		return ipcSocketfd;
	}
	void setIpcSocketfd(int ipcSocketfd){
		this -> ipcSocketfd = ipcSocketfd ;
	}

	int getId(){
		return id ;
	}
	void setId(int id){
		this -> id = id;
	}
	
	int getPid(){
		return pid;
	}
	void setPid(int pid){
		this -> pid = pid;
	}

	string getName(){
		return name ;
	}
	void setName(string name){
		this -> name = name;
	}

	string getIpAddress(){
		return ipAddress ;
	}
	void setIpAddress(string ipAddress){
		this -> ipAddress = ipAddress ;
	}

	int getPort(){
		return port;
	}
	void setPort(int port){
		this -> port = port ;
	}
};

void callPrintenv(string envVar,int mSocket);
void callSetenv(string envVar,string value);
void singleProcess(vector<string>commandVec,bool hasNumberPipe,bool bothStderr,int pipeAfterLine,int numberPipe[PET_SIZE][2],int pipe_expired_table[PET_SIZE],int slaveSocket);
void multiProcess(vector<string>commandVec,int process_count,bool hasNumberPipe,bool bothStderr,int pipeAfterLine,int numberPipe[PET_SIZE][2],int pipe_expired_table[PET_SIZE],int slaveSocket);

void PET_init(int pipe_expired_table[PET_SIZE]);
void PET_iterate(int pipe_expired_table[PET_SIZE]); 
int PET_findExpired(int pipe_expired_table[PET_SIZE]);
int PET_findSameLine(int pipe_expired_table[PET_SIZE],int target_line);
int PET_emptyPipeIndex(int pipe_expired_table[PET_SIZE]);
vector<int> PET_existPipe(int pipe_expired_table[PET_SIZE]);

vector<int> existUser(User userlist[MAX_USERNUMBER]);
int emptyUserIndex(User userlist[MAX_USERNUMBER]);

void wait4children(int signo);
string extractClientInput(char buffer[MAX_LENGTH],int readCount);

int main(int argc, char *argv[]) {
	string input ="" ; 
	string aWord ="" ;
	stringstream ss;
	vector<string> commandVec;
	int child_done_status ;
	pid_t child_done_pid ;
	pid_t fork_pid ;
 
 	int numberPipe[PET_SIZE][2];
	int pipe_expired_table[PET_SIZE];
	bool hasNumberPipe = false;
	bool bothStderr = false;
	int pipeAfterLine =0 ;

	// Socket setting
	int masterSocket,slaveSocket,clientLen,readCount ;
	struct sockaddr_in clientAddr , serverAddr;
	char buffer[MAX_LENGTH] ={} ;
	string promptString ="% ";
	char promptBuffer[3] ={'%',' ','\0'};
	bool bReuseAddr= true;
	int port = stoi(argv[1]); 

	// Select & IpcSocket setting
	User userlist[MAX_USERNUMBER];
	int selectRtnVal ;
	fd_set rfds,afds;
	int nfdp = getdtablesize();
	FD_ZERO(&rfds);
	FD_ZERO(&afds);
	
	int ipcSocket,ipcSlaveSocket =-1;
	struct sockaddr_un unSAddr;

	// Pipe expired table initialization.
	PET_init(pipe_expired_table);

	// set $PATH to bin/ ./ initially
	callSetenv("PATH","bin:.");

	// Master socket setting 
	if((masterSocket = socket(AF_INET,SOCK_STREAM,0))<0){
		cerr << "master socket create Fail\n";	
	}else{
		FD_SET(masterSocket,&afds);
	}
	bzero(&serverAddr,sizeof(serverAddr));
	serverAddr.sin_family = AF_INET ;
	serverAddr.sin_addr.s_addr =htonl(INADDR_ANY);
	serverAddr.sin_port = htons(port);
	
	setsockopt(masterSocket,SOL_SOCKET,SO_REUSEADDR,(const char*)&bReuseAddr,sizeof(bool));

	if(bind(masterSocket,(struct sockaddr *)&serverAddr,sizeof(serverAddr))<0){
		cerr <<"master socket bind Fail\n" ;
	}
	
	cout <<"Server is listening...\n";
	listen(masterSocket,5);

	while(true){
		memcpy(&rfds,&afds,sizeof(rfds));
		selectRtnVal = select(nfdp,&rfds,(fd_set *)0,(fd_set *)0,(struct timeval *)0);
		
		if(selectRtnVal <0){
			continue;
		}else{
			// selectRtnVal >0  Now check which fd is on
			// if masterSocket is on
			if(FD_ISSET(masterSocket,&rfds)){
				// develop							
				cout << "masterSocket rfds is on\n";			
				clientLen = sizeof(clientAddr);	
				slaveSocket = accept(masterSocket,(struct sockaddr *)&clientAddr,(socklen_t *)&clientLen);
				if(slaveSocket <0){
					cerr << "masterSocket accept error\n";
				}else{
					// server connect success
					int sockForkPid;
					int emptyIndex = emptyUserIndex(userlist);
					cout <<"slaveSocket created Success\n";
					
					if((sockForkPid = fork()) <0){
						cerr << "fork child error\n";
					}else if(sockForkPid >0){ // Parent
						// set user information in userlist
						userlist[emptyIndex].setAvailable(true);
						userlist[emptyIndex].setId(emptyIndex+1);
						userlist[emptyIndex].setPid(sockForkPid);
						userlist[emptyIndex].setIpAddress(string(inet_ntoa(clientAddr.sin_addr)));
						userlist[emptyIndex].setPort((int)ntohs(clientAddr.sin_port));

						// signal regist for child exit and close slaveSocket
						signal(SIGCHLD,wait4children);
						close(slaveSocket);
						
						// Parent need to be IPC Server
						if((ipcSocket = socket(AF_UNIX,SOCK_STREAM,0)) < 0){
							cerr << "ipcSocket create fail\n" ;
						}

						string ipcSocketPath="./user_pipe/ipcID"+to_string(emptyIndex+1)+".sock"; 
						unlink(ipcSocketPath.c_str());
						memset(&unSAddr,0,sizeof(unSAddr));
						unSAddr.sun_family = AF_UNIX;
						strncpy(unSAddr.sun_path,ipcSocketPath.c_str(),sizeof(unSAddr.sun_path)-1);

						if(bind(ipcSocket,(const struct sockaddr*)&unSAddr,sizeof(unSAddr))==-1){
							cerr << "parent ipc bind error: " << errno <<"\n";
						}
						cout << "Parent ipcSocket listening...\n";
						listen(ipcSocket,5);
						cout << "Parent ipcSocket is ready to accept\n";
						ipcSlaveSocket = accept(ipcSocket,NULL,NULL);

						if(ipcSlaveSocket <0){
							cout << "ipcSocket accept error\n";
							close(ipcSocket);
							ipcSocket =-1 ;
							ipcSlaveSocket = -1;

						}else{
							cout << "ipcSocket accept and fd: "<< ipcSlaveSocket <<"\n";
							userlist[emptyIndex].setIpcSocketfd(ipcSlaveSocket);
							close(ipcSocket);
							ipcSocket =-1;
							ipcSlaveSocket =-1;
							// After connected -> select need to monitor this ipcSlaveSocket
							FD_SET(userlist[emptyIndex].getIpcSocketfd(),&afds);
							cout << "userlist[" << emptyIndex <<"].ipcSocketfd="<<to_string(userlist[emptyIndex].getIpcSocketfd()) <<" is set to &afds\n";

						}

					}else if (sockForkPid ==0){ //Child
						// close useless socket
						close(masterSocket);
						vector<int> existUserIndex = existUser(userlist);
						for(int i=0;i<existUserIndex.size();i++){
							if(userlist[existUserIndex[i]].getIpcSocketfd() != -1){
								close(userlist[existUserIndex[i]].getIpcSocketfd());
								userlist[existUserIndex[i]].reset();
							}
						}
						// Ready to connect to parent's ipcSocket	
						if((ipcSocket = socket(AF_UNIX,SOCK_STREAM,0)) <0){
							cerr <<"child ipcSocket create fail\n";
						}
						memset(&unSAddr,0,sizeof(unSAddr));
						unSAddr.sun_family = AF_UNIX;
						string ipcSocketPath="./user_pipe/ipcID"+to_string(emptyIndex+1)+".sock"; 
						strncpy(unSAddr.sun_path,ipcSocketPath.c_str(),sizeof(unSAddr.sun_path)-1);
						while(connect(ipcSocket,(const struct sockaddr *)&unSAddr,sizeof(unSAddr)) <0){
							cerr << "Child ipcScoket connect error: " << errno <<"\n";
							cerr << "Server may not create the server side ipcSocket\n";
							cerr << "sleep for 1 sec and try to connect again\n";
							sleep(1);
						}

						// connect success ->send (1)Welcome (2)broadcast (3) prompt
						// (1) welcome
						string welcome_1 = "****************************************\n";
						string welcome_2 = "** Welcome to the information server. **\n";
						write(slaveSocket,welcome_1.c_str(),welcome_1.length());
						write(slaveSocket,welcome_2.c_str(),welcome_2.length());
						write(slaveSocket,welcome_1.c_str(),welcome_1.length());
						// develop -> (2) broadcast -> tell Parent to broadcast
						// (3) prompt
						write(slaveSocket,promptString.c_str(),promptString.length());

						// wait for client input.
						while(readCount = read(slaveSocket,buffer,sizeof(buffer))){
							// develop -> execute variable function acoording to command
							input = extractClientInput(buffer,readCount);
							cout << "*** Child " << emptyIndex+1 << " says \'" << input <<"\' ***\n" ;
							
							// Start to handle the input
							hasNumberPipe = false;
							bothStderr = false;
							pipeAfterLine =0 ;
					
							ss << input ;
					    	while (ss >> aWord) {
								commandVec.push_back(aWord);
							}
							// each round except for empty command  -> PET_iterate() 
							if(commandVec.size()!=0){
								PET_iterate(pipe_expired_table);
							}
					
							// We want to check if the command is the three built-in command
							if(commandVec.size()!=0 && commandVec[0]=="exit"){
								break ;
							}else if(commandVec.size()!=0 && commandVec[0]=="printenv"){
								if(commandVec.size()==2){
									callPrintenv(commandVec[1],slaveSocket);     
								} 
							}else if(commandVec.size()!=0 && commandVec[0]=="setenv"){
								if(commandVec.size()==3){
									callSetenv(commandVec[1],commandVec[2]);    
								}
							}else if(commandVec.size()!=0){ // The last condition is not empty.
								// Not the three built-in command
								// Ready to handle the command. 
					
								// We want to know how much process need to call fork()
								// And check if there is number pipe -> flag on.
					      		int process_count =1 ;
					      		for(int i=0;i<commandVec.size();i++){
					        		if(commandVec[i].find("|")!= string::npos){ //Really find '|' in this element
					          			if(commandVec[i].length()==1){ // '|' Pipe only
					            			process_count ++ ;
					          			}else{ // it is number pipe
											// hasNumberPipe flag on and set the pipeAfterLine
											hasNumberPipe = true ;
											pipeAfterLine = stoi(commandVec[i].substr(1));
					          			}
					        		}else if(commandVec[i].find("!")!= string::npos){//find '!' in this element
					      				hasNumberPipe = true;
										bothStderr = true;
										pipeAfterLine = stoi(commandVec[i].substr(1)) ;
									}
								} 
								
								// Now we have the number of processes 
								// we can start to construct the pipe.
								if(process_count ==1){
									singleProcess(commandVec,hasNumberPipe,bothStderr,pipeAfterLine,numberPipe,pipe_expired_table,slaveSocket);	
								}else if(process_count>=2){
									multiProcess(commandVec,process_count,hasNumberPipe,bothStderr,pipeAfterLine,numberPipe,pipe_expired_table,slaveSocket);	
								}		
							}
					
							//one term command is done -> Initialize it.
							commandVec.clear();
							ss.str("");
							ss.clear();
							write(slaveSocket,promptBuffer,2);
						}
						// client had left.
						close(ipcSocket);
						close(slaveSocket);
						exit(0);
					}
				}
			}

			// if any ipcSocket is on
			vector<int> existUserIndex = existUser(userlist);
			for(int i=0;i<existUserIndex.size();i++){
				if(FD_ISSET(userlist[existUserIndex[i]].getIpcSocketfd(),&rfds)){
					// get some message from userlist[existUserIndex[i]].socketfd
					cout << "userlist["<<existUserIndex[i]<<"].socketfd rfds ON\n";
					while((readCount = read(userlist[existUserIndex[i]].getIpcSocketfd(),buffer,sizeof(buffer))) == -1 ){
						cout << "*** readCount == -1 / might be interrupted bt signal ***\n";
						cout << "*** handle it -> read again ***\n";
					}
					if(readCount ==0){
						// readCount ==0 means the process might dead.
						cout <<userlist[existUserIndex[i]].getId()<<"\'s process dead\n"; 	

						// develop -> broadcast to everyone userlist[existUserIndex[i]] left ;

						FD_CLR(userlist[existUserIndex[i]].getIpcSocketfd(),&afds);
						close(userlist[existUserIndex[i]].getIpcSocketfd());
						userlist[existUserIndex[i]].reset();

					}else{
						// readCount >0 this process wanna say something
						// in this process there are 4 kinds of command need it.
						// who / name / tell / yell 
						input = extractClientInput(buffer,readCount);
						cout << userlist[existUserIndex[i]].getId() <<"\'s Process sends \'" <<input << "\' to Parent\n";
						// develop - who name tell yell broadcast 
					}
				}
			}
		}
	}
	close(masterSocket);
	return 0;
}
// Single process handle 
void singleProcess(vector<string> commandVec,bool hasNumberPipe,bool bothStderr,int pipeAfterLine,int numberPipe[PET_SIZE][2],int pipe_expired_table[PET_SIZE],int slaveSocket){
	pid_t child_done_pid;
	int child_done_status;
	bool needRedirection =false;
	string redirectionFileName ="";
	char* arg[MAX_LENGTH];

	// If number pipe expired -> remember to handle it.
	int newNumberPipeIndex;
	int pipeToSameLine;
	int expiredIndex = PET_findExpired(pipe_expired_table);	
	vector<int> existPipeIndex = PET_existPipe(pipe_expired_table); 
	
	// Check if there is redirection command
	for(int i=0;i<commandVec.size();i++){
		if(commandVec[i].find(">")!= string::npos){
			// There is a ">" in command
			// Now we check if there is a file name after ">"
			if((i+1)<commandVec.size()){
				needRedirection = true ;
				redirectionFileName = commandVec[i+1] ;
				break;
			}else{
				string temp = "syntax error near unexpected token\n" ;
				cerr << temp ;
				write(slaveSocket,temp.c_str(),temp.length()) ;
				return;
			}
		}
	}
	
	// If this command need to number pipe
	// First check if there is any pipe want to write to the same line
	// If No -> create the pipe and set expired_table
	if(hasNumberPipe == true){
		pipeToSameLine = PET_findSameLine(pipe_expired_table,pipeAfterLine);
		if(pipeToSameLine == -1){ // there isn't any  pipe want to write to the same line.	
			newNumberPipeIndex = PET_emptyPipeIndex(pipe_expired_table);
			pipe(numberPipe[newNumberPipeIndex]);
			pipe_expired_table[newNumberPipeIndex] = pipeAfterLine ;
		}
	}	

	pid_t fork_pid = fork();

	if(fork_pid ==-1){ //fork Error
  		cout <<"fork error\n" ;
    }else if(fork_pid ==0){ // Child
   		// handle execvp argument
        for(int i=0;i<commandVec.size();i++){
			if(commandVec[i].find(">")==string::npos && commandVec[i].find("|")==string::npos && commandVec[i].find("!")==string::npos){
        		arg[i] = strdup(commandVec[i].c_str());
			}else{
				//Find ">" or Find "|" : we abort it.
				break ;
			}
      	}
		//if there is number pipe expired
		if(expiredIndex != -1){
			// set pipe to STDIN and close the pipe
			close(numberPipe[expiredIndex][1]);
			dup2(numberPipe[expiredIndex][0],STDIN_FILENO);
			close(numberPipe[expiredIndex][0]);
		}		
			
		//if this child need to number pipe to another line
		if(hasNumberPipe==true){
			// dup socket to STDERR_FILNO
			dup2(slaveSocket,STDERR_FILENO);
			close(slaveSocket);

			if(pipeToSameLine == -1){
				// Use the new Pipe.
				close(numberPipe[newNumberPipeIndex][0]); //close read
				dup2(numberPipe[newNumberPipeIndex][1],STDOUT_FILENO); //dup write to STDOUT_FILENO
				if(bothStderr == true){
					dup2(numberPipe[newNumberPipeIndex][1],STDERR_FILENO);
				}
				close(numberPipe[newNumberPipeIndex][1]); //close write
			}else{
				// Write to the old pipe.
				dup2(numberPipe[pipeToSameLine][1],STDOUT_FILENO);
				if(bothStderr == true ){
					dup2(numberPipe[pipeToSameLine][1],STDERR_FILENO);
				}
			}
		}else{
			// no number pipe -> output to socket
			dup2(slaveSocket,STDOUT_FILENO);
			dup2(slaveSocket,STDERR_FILENO);
			close(slaveSocket) ;
		}
	
		//if need to redirection -> reset the STDOUT to a file
		if(needRedirection){
			int fd = open(redirectionFileName.c_str(), O_WRONLY|O_CREAT|O_TRUNC ,S_IRUSR|S_IWUSR);
			dup2(fd,STDOUT_FILENO);
			close(fd);
		}
		
		//Before execvp -> Child closes the useless number  pipe
		for(int i=0;i<existPipeIndex.size();i++){
			close(numberPipe[existPipeIndex[i]][0]);
			close(numberPipe[existPipeIndex[i]][1]);
		}

   		// Ready to execvp
       	if(execvp(arg[0],arg)==-1){ // execvp fail
			string temp = "Unknown command: [" + string(arg[0]) + "].\n";
			cerr << temp ;
			// Also need to write to socket
       		exit(10);
        }	
	}else if(fork_pid >0){ //Parent
		// Parent need tidy expired number pipe
		if(expiredIndex != -1){
			close(numberPipe[expiredIndex][0]);
			close(numberPipe[expiredIndex][1]);
		}

		// If this command has number Pipe
		// Parent doesn't need to wait child DONE.
		// otherwise need to wait.
		if(hasNumberPipe == true){
			signal(SIGCHLD,wait4children);
		}else{
			waitpid(fork_pid,NULL,0);
		}
	}

}

// Multi process handle
void multiProcess(vector<string>commandVec,int process_count,bool hasNumberPipe,bool bothStderr,int pipeAfterLine,int numberPipe[PET_SIZE][2],int pipe_expired_table[PET_SIZE],int slaveSocket){
	// First handle all of the command and it's argument
	int process_index =0;
	int process_cmd_index=0;
	pid_t child_done_pid;
	pid_t fork_pid[2];
	int child_done_status;
	bool needRedirection =false ;
	string redirectionFileName = "" ;
	char* arg[process_count][256]={NULL} ;
	
	// If number pipe expired -> remember to handle it.
	int newNumberPipeIndex ;
	int pipeToSameLine ;
	int expiredIndex = PET_findExpired(pipe_expired_table);	
	vector<int> existPipeIndex = PET_existPipe(pipe_expired_table); 
	
	// Handle argument and file redirection setting.
	for(int cmd_index=0; cmd_index<commandVec.size() ; cmd_index++){
		if( commandVec[cmd_index].find("|")!= string::npos){ //Find | in this string
			if(commandVec[cmd_index].length() ==1){
				// single "|"
				process_index ++;
				process_cmd_index =0 ;
			}
		}else if(commandVec[cmd_index].find(">")!= string::npos){ //Find > in this string
			if((cmd_index+1) < commandVec.size()){ 
				needRedirection = true ;
				redirectionFileName = commandVec[cmd_index+1];
				break ;
			}else{
				string temp = "syntax error near unexpected token\n";
				cerr << temp;
				write(slaveSocket,temp.c_str(),temp.length()) ;
				return ;
			}		
		}else if(commandVec[cmd_index].find("!")!= string::npos){ //Find ! in this string
			break ;
		}else{
			arg[process_index][process_cmd_index] = strdup(commandVec[cmd_index].c_str());
			process_cmd_index ++ ;
		}		
	}

	// All argument of process is handled.
	// Now we are going to create the pipe.
	int mPipe[2][2];
	process_index =0 ;

	if(process_count ==2){
		pipe(mPipe[0]);	

		if((fork_pid[0]=fork()) == -1){
			cout <<"Multiprocess(...)-process_count=2 : fork error\n";
		}else if(fork_pid[0] ==0){ //child1
			//if there is number pipe expired
			if(expiredIndex != -1){
				// set pipe to STDIN and close the pipe
				close(numberPipe[expiredIndex][1]);
				dup2(numberPipe[expiredIndex][0],STDIN_FILENO);
				close(numberPipe[expiredIndex][0]);
			}		

			// Child1 need to modify the stdout
			close(mPipe[0][0]);
			dup2(mPipe[0][1],STDOUT_FILENO);
			close(mPipe[0][1]);
			
			// Child 1 doesn't need slaveSocket
			// But STDERR may need it so -> dup to STDERR
			dup2(slaveSocket,STDERR_FILENO);
			close(slaveSocket);

			// Before execvp -> close useless number Pipe
			for(int i=0;i<existPipeIndex.size();i++){
				close(numberPipe[existPipeIndex[i]][0]);
				close(numberPipe[existPipeIndex[i]][1]);
			}

			// Ready to execvp
			if(execvp(arg[0][0],arg[0]) == -1){ // execvp fail (maybe bug)
				string temp ="Unknown command: ["+ string(arg[0][0]) +"].\n";
				cerr << temp ;
				exit(0);
			}

		}else if(fork_pid[0] >0) { //Parent
			// Parent need to tidy expired number pipe
			if(expiredIndex != -1){
				close(numberPipe[expiredIndex][0]);
				close(numberPipe[expiredIndex][1]);
			}

			// If this command need to number pipe
			// First check if there is any pipe want to write to the same line
			// If No -> create the pipe and set expired_table
			if(hasNumberPipe == true){
				pipeToSameLine = PET_findSameLine(pipe_expired_table,pipeAfterLine);
				if(pipeToSameLine == -1){ // there isn't any  pipe want to write to the same line.	
					newNumberPipeIndex = PET_emptyPipeIndex(pipe_expired_table);
					pipe(numberPipe[newNumberPipeIndex]);
					pipe_expired_table[newNumberPipeIndex] = pipeAfterLine ;
				}
			}

			if( (fork_pid[1]=fork()) ==-1){
				cout <<"Multiprocess(...)-process_count=2 : fork error2\n";
			}else if(fork_pid[1] ==0){ //child2
				close(mPipe[0][1]);
				dup2(mPipe[0][0],STDIN_FILENO);
				close(mPipe[0][0]);

				//if this child need to number pipe to another line
				if(hasNumberPipe==true){
					//dup socket to STDERR_FILENO
					dup2(slaveSocket,STDERR_FILENO);
					close(slaveSocket);

					if(pipeToSameLine == -1){
						// Use the new Pipe.
						close(numberPipe[newNumberPipeIndex][0]); //close read
						dup2(numberPipe[newNumberPipeIndex][1],STDOUT_FILENO); //dup write to STDOUT_FILENO
						if(bothStderr == true){
							dup2(numberPipe[newNumberPipeIndex][1],STDERR_FILENO);
						}
						close(numberPipe[newNumberPipeIndex][1]); //close write
					}else{
						// Write to the old pipe.
						dup2(numberPipe[pipeToSameLine][1],STDOUT_FILENO);
						if(bothStderr == true ){
							dup2(numberPipe[pipeToSameLine][1],STDERR_FILENO);
						}
					}
				}else{
					// no number pipi -> output to socket
					dup2(slaveSocket,STDOUT_FILENO);
					dup2(slaveSocket,STDERR_FILENO);
					close(slaveSocket);
				}

				if(needRedirection){
     				int fd = open(redirectionFileName.c_str(),O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR);
     				dup2(fd,STDOUT_FILENO);
     				close(fd);
				}
				
				// Before execvp -> close useless number pipe
				for(int i=0;i<existPipeIndex.size();i++){
					close(numberPipe[existPipeIndex[i]][0]);
					close(numberPipe[existPipeIndex[i]][1]);
				}

				//Ready to execvp
				if(execvp(arg[1][0],arg[1]) == -1){
					cerr <<"Unknown command: ["<<arg[1][0]<<"].\n";
					exit(0);
				}

			}else if(fork_pid[1] >0){ //parent
				close(mPipe[0][0]);
				close(mPipe[0][1]);				

				if(hasNumberPipe == true){
					// Needless to wait the process
					signal(SIGCHLD,wait4children);
				}else{
					wait(NULL);
					wait(NULL);
				}
			} 	
		}

	}else if(process_count >2){
		process_index=0;
		pipe(mPipe[0]);
		
		if((fork_pid[0]=fork()) == -1){
			cout <<"Multiprocess(...)-process_count>2 : fork error\n";
		}else if(fork_pid[0] ==0){ //child1
			//if there is number pipe expired
			if(expiredIndex != -1){
				// set pipe to STDIN and close the pipe
				close(numberPipe[expiredIndex][1]);
				dup2(numberPipe[expiredIndex][0],STDIN_FILENO);
				close(numberPipe[expiredIndex][0]);
			}

			// Child1 need to modify the stdout
			close(mPipe[0][0]);
			dup2(mPipe[0][1],STDOUT_FILENO);
			close(mPipe[0][1]);
			
			// Child1 doesn't need slaveSocket but STDERR may need
			dup2(slaveSocket,STDERR_FILENO);
			close(slaveSocket);

			// Before execvp -> close useless number Pipe
			for(int i=0;i<existPipeIndex.size();i++){
				close(numberPipe[existPipeIndex[i]][0]);
				close(numberPipe[existPipeIndex[i]][1]);
			}

			// Ready to execvp
			if(execvp(arg[process_index][0],arg[process_index]) == -1){
				string temp = "Unknown command: [" + string(arg[process_index][0]) + "].\n";
				cerr << temp ;
				exit(0);
			}

		}else if(fork_pid[0] >0) { //Parent
			process_index++;
			// need to regist signal for the first child
			signal(SIGCHLD,wait4children);

			// Parent need to tidy expired number pipe
			if(expiredIndex != -1){
				close(numberPipe[expiredIndex][0]);
				close(numberPipe[expiredIndex][1]);
			}

			// Handle Process 2,3,4,5,....,n-1 in the middle.
			for(int i=1;i<process_count-1;i++){
				pipe(mPipe[process_index%2]);

				if( (fork_pid[process_index%2]=fork()) ==-1){
					// might handle it
					i=i-1;
					process_index--;
				}else if(fork_pid[process_index%2] ==0){ //child
					close(mPipe[(process_index-1)%2][1]); //close front write.
					dup2(mPipe[(process_index-1)%2][0],STDIN_FILENO ); //dup front read to STDIN
					close(mPipe[(process_index-1)%2][0]); //close front read.
					
					close(mPipe[process_index%2][0]); //close behind read
					dup2(mPipe[process_index%2][1],STDOUT_FILENO); //dup behind write to STDOUT
					close(mPipe[process_index%2][1]); //close behind write
					
					// Process in the middle doesn't need slaveSocket but STDERR may need
					dup2(slaveSocket,STDERR_FILENO);
					close(slaveSocket);

					// Before execvp -> close useless number pipe
					for(int i=0;i<existPipeIndex.size();i++){
						close(numberPipe[existPipeIndex[i]][0]);
						close(numberPipe[existPipeIndex[i]][1]);
					}

					// Ready to execvp
					if(execvp(arg[process_index][0],arg[process_index])==-1){
						cerr <<"Unknown command: ["<<arg[process_index][0] <<"].\n";
						exit(0);
					}	

				}else if(fork_pid[process_index%2] >0){ //Parent
					// Close front pipe .
					close(mPipe[(process_index-1)%2][0]);
					close(mPipe[(process_index-1)%2][1]);
					
					// Maybe need to regist signal for each child in the middle.
					signal(SIGCHLD,wait4children);
				}
				process_index++ ;
			}
			
			// Handle Process n which is the last one in process.
			pid_t last_process ;
			
			// If this command need to number pipe
			// First check if there is any pipe want to write to the same line
			// If No -> create the pipe and set expired_table
			if(hasNumberPipe == true){
				pipeToSameLine = PET_findSameLine(pipe_expired_table,pipeAfterLine);
				if(pipeToSameLine == -1){ // there isn't any  pipe want to write to the same line.	
					newNumberPipeIndex = PET_emptyPipeIndex(pipe_expired_table);
					pipe(numberPipe[newNumberPipeIndex]);
					pipe_expired_table[newNumberPipeIndex] = pipeAfterLine ;
				}
			}

			if((last_process=fork())==-1){ //
				cout <<"last process fork error:"<<process_index<<"\n";
			}else if(last_process ==0){ //child
				//if this child need to number pipe to another line
				if(hasNumberPipe==true){
					// dup socket to STDERR_FILENO
					dup2(slaveSocket,STDERR_FILENO);
					close(slaveSocket);

					if(pipeToSameLine == -1){
						// Use the new Pipe.
						close(numberPipe[newNumberPipeIndex][0]); //close read
						dup2(numberPipe[newNumberPipeIndex][1],STDOUT_FILENO); //dup write to STDOUT_FILENO
						if(bothStderr == true){
							dup2(numberPipe[newNumberPipeIndex][1],STDERR_FILENO);
						}
						close(numberPipe[newNumberPipeIndex][1]); //close write
					}else{
						// Write to the old pipe.
						dup2(numberPipe[pipeToSameLine][1],STDOUT_FILENO);
						if(bothStderr == true ){
							dup2(numberPipe[pipeToSameLine][1],STDERR_FILENO);
						}
					}
				}else{
					// no number pipe -> output to socket
					dup2(slaveSocket,STDOUT_FILENO);
					dup2(slaveSocket,STDERR_FILENO);
					close(slaveSocket);
				}
			
				close(mPipe[(process_index-1)%2][1]); //close front write
				dup2(mPipe[(process_index-1)%2][0],STDIN_FILENO); //dup front read to STDIN
				close(mPipe[(process_index-1)%2][0]); // close front read
				
				if(needRedirection){
     				int fd = open(redirectionFileName.c_str(),O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR);
     				dup2(fd,STDOUT_FILENO);
     				close(fd);
				}

				// Before execvp -> close useless number pipe
				for(int i=0;i<existPipeIndex.size();i++){
					close(numberPipe[existPipeIndex[i]][0]);
					close(numberPipe[existPipeIndex[i]][1]);
				}

				//Ready to execvp
				if(execvp(arg[process_index][0],arg[process_index])){
					cerr <<"Unknown command: ["<< arg[process_index][0] <<"].\n";
					exit(0);
				}

			}else if(last_process >0){ //Parent
				// Close front pipe.
				close(mPipe[(process_index-1)%2][0]);
				close(mPipe[(process_index-1)%2][1]);
				// Last process we can use wait -> SHELL will stop here
				// Until the last process done
				if(hasNumberPipe ==true){
					// needless to wait
					signal(SIGCHLD,wait4children);
				}else{
					// wait the last process
					waitpid(last_process,NULL,0);
				}
			}
		}
	}			
}

// Number pipe function.
void PET_init(int pipe_expired_table[PET_SIZE]){
	for(int i=0;i<PET_SIZE;i++){
		pipe_expired_table[i] = -1 ;
	}
}

void PET_iterate(int pipe_expired_table[PET_SIZE]){
	for(int i=0;i<PET_SIZE;i++){
		if(pipe_expired_table[i]!=-1){
			pipe_expired_table[i] = pipe_expired_table[i]-1;
		}		
	}
}

int PET_findExpired(int pipe_expired_table[PET_SIZE]){
	int result = -1;
	for(int i=0;i<PET_SIZE;i++){
		if(pipe_expired_table[i]==0){
			result =i;
			break ;
		}	
	}
	return result ;
}


int PET_findSameLine(int pipe_expired_table[PET_SIZE],int target_line){
	int result = -1;
	for(int i=0;i<PET_SIZE;i++){
		if(pipe_expired_table[i]==target_line){
			result = i;
			break ;
		}			
	}
	return result;
}

int PET_emptyPipeIndex(int pipe_expired_table[PET_SIZE]){
	int result = -1;
	
	for(int i=0;i<PET_SIZE;i++){
		if(pipe_expired_table[i]==-1){
			result = i;
			break ;
		}
	}
	return result ;
}

vector<int> PET_existPipe(int pipe_expired_table[PET_SIZE]){
	vector<int> result ;
	for(int i=0;i<PET_SIZE;i++){
		if(pipe_expired_table[i]!= 0 && pipe_expired_table[i]!= -1){ // Except empty:-1 ,expired:0
			result.push_back(i);
		}
	}
	return result ;
}

// three built-in commands(setenv,printenv,exit)
void callPrintenv(string envVar,int mSocket){
	const char* input = envVar.c_str() ;
	char* path_string = getenv (input);

	if (path_string!=NULL){
		cout <<path_string<<endl;
		string typeS(path_string);
		typeS += '\n';
		
		// Use socket to transfer data
		write(mSocket,typeS.c_str(),typeS.length());
	}
}

void callSetenv(string envVar,string value){
	const char *envname = envVar.c_str();
	const char *envval = value.c_str();
	setenv(envname,envval,1);
}
// User functions
vector<int> existUser(User userlist[MAX_USERNUMBER]){
	vector<int> result ;
	for(int i=0;i<MAX_USERNUMBER;i++){
		if(userlist[i].getAvailable() == true){
			result.push_back(i);
		}
	}
	return result ;
}

int emptyUserIndex(User userlist[MAX_USERNUMBER]){
	int result =-1;
	
	for(int i=0;i<MAX_USERNUMBER;i++){
		if(userlist[i].getAvailable() == false){
			result = i;
			break;
		}
	}
	return result;
}
// signal handler
void wait4children(int signo){
	int status;
	while(waitpid(-1,&status,WNOHANG)>0);
}
string extractClientInput(char buffer[MAX_LENGTH],int readCount){
	string result ="" ;
	
	for(int i=0;i<readCount;i++){
		if(buffer[i]=='\n' || buffer[i]=='\r' || buffer[i]=='\0'){
			break ;
		}
		result += buffer[i] ;
	}
	return result; 
}
