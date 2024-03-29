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
	int userPipeFd[MAX_USERNUMBER];

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
		userPipeFdInit();
	}

	bool getAvailable(){
		return available ;
	}
	void setAvailable(bool available){
		this -> available = available;
	}

	void userPipeFdInit(){
		for(int i=0;i<MAX_USERNUMBER;i++){
			userPipeFd[i] =-1;
		}
	}
	int getUserPipeFd(int id){
		return userPipeFd[id-1];
	}
	void setUserPipeFd(int id,int fd){
		userPipeFd[id-1] = fd ;
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
void singleProcess(vector<string>commandVec,bool hasNumberPipe,bool bothStderr,int pipeAfterLine,int numberPipe[PET_SIZE][2],int pipe_expired_table[PET_SIZE],int slaveSocket,bool hasUserPipeFrom,bool hasUserPipeTo,int userPipeFrom);
void multiProcess(vector<string>commandVec,int process_count,bool hasNumberPipe,bool bothStderr,int pipeAfterLine,int numberPipe[PET_SIZE][2],int pipe_expired_table[PET_SIZE],int slaveSocket,bool hasUserPipeFrom,bool hasUserPipeTo,int userPipeFrom);

void PET_init(int pipe_expired_table[PET_SIZE]);
void PET_iterate(int pipe_expired_table[PET_SIZE]); 
int PET_findExpired(int pipe_expired_table[PET_SIZE]);
int PET_findSameLine(int pipe_expired_table[PET_SIZE],int target_line);
int PET_emptyPipeIndex(int pipe_expired_table[PET_SIZE]);
vector<int> PET_existPipe(int pipe_expired_table[PET_SIZE]);

vector<int> existUser(User userlist[MAX_USERNUMBER]);
int emptyUserIndex(User userlist[MAX_USERNUMBER]);

string extractClientInput(char buffer[MAX_LENGTH],int readCount);

void wait4children(int signo);
void sig_usr(int signo);

int gb_ipcSocket =-1;
int gb_slaveSocket =-1;
bool gb_userPipeFromSuccess = false;
bool gb_userPipeToSuccess = false;
int gb_openWriteFd =-1;
int gb_openReadFd[MAX_USERNUMBER] ;

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
	
	// userPipe setting
	bool hasUserPipeFrom = false;
	int userPipeFrom = -1 ; 
	bool hasUserPipeTo =false;
	int userPipeTo = -1;
	for(int j=0;j<MAX_USERNUMBER;j++){
		gb_openReadFd[j] =-1;
	}
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
					
					// set user information in userlist
					userlist[emptyIndex].setAvailable(true);
					userlist[emptyIndex].setId(emptyIndex+1);
					userlist[emptyIndex].setIpAddress(string(inet_ntoa(clientAddr.sin_addr)));
					userlist[emptyIndex].setPort((int)ntohs(clientAddr.sin_port));
					
					if((sockForkPid = fork()) <0){
						cerr << "fork child error\n";
					}else if(sockForkPid >0){ // Parent
						// setting child Pid
						userlist[emptyIndex].setPid(sockForkPid);

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
						// set my pid
						userlist[emptyIndex].setPid((int)getpid());

						// for child -> reset global variable:  gb_slaveSocket and set singal handler
						gb_slaveSocket = slaveSocket ;
						signal(SIGUSR1,sig_usr);
						signal(SIGUSR2,sig_usr);

						// close useless socket and users except "itself" !!
						close(masterSocket);
						vector<int> existUserIndex = existUser(userlist);
						for(int i=0;i<existUserIndex.size();i++){
							if(existUserIndex[i] != emptyIndex){	
								if(userlist[existUserIndex[i]].getIpcSocketfd() != -1){
									close(userlist[existUserIndex[i]].getIpcSocketfd());
									userlist[existUserIndex[i]].reset();
								}
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
							
						// for child -> reset global variable:  gb_ipcSocket
						gb_ipcSocket = ipcSocket ;

						// connect success ->send (1)Welcome (2)broadcast (3) prompt
						// (1) welcome
						string welcome_1 = "****************************************\n";
						string welcome_2 = "** Welcome to the information server. **\n";
						write(slaveSocket,welcome_1.c_str(),welcome_1.length());
						write(slaveSocket,welcome_2.c_str(),welcome_2.length());
						write(slaveSocket,welcome_1.c_str(),welcome_1.length());
						// (2) broadcast -> tell Parent to broadcast
						string loginMessage = "login";
						write(ipcSocket,loginMessage.c_str(),loginMessage.length());
						//loginMessage = "*** User \'"+userlist[emptyIndex].getName()+"\' entered from "+ userlist[emptyIndex].getIpAddress()+":" +to_string(userlist[emptyIndex].getPort())+". ***\n";
						//write(slaveSocket,loginMessage.c_str(),loginMessage.length());
						// now use this way : parent also signal to me						
						pause(); 

						// (3) prompt
						write(slaveSocket,promptString.c_str(),promptString.length());	
						// wait for client input.
						while(true){		
							readCount = read(slaveSocket,buffer,sizeof(buffer));
							if(readCount ==0){
								// we close ipcSocket directly -> parent will know we leave
								// and execute the corresponding broadcast
								break ;

							}else if(readCount ==-1){
								cout << "*** Child "<<emptyIndex+1 <<" was interrupted by signal\n";
								cout << "use while loop to go back to read message from client\n";
								continue;

							}else{	
								// develop -> execute variable function acoording to command
								input = extractClientInput(buffer,readCount);
								cout << "*** Child " << emptyIndex+1 << " says \'" << input <<"\' ***\n" ;
								
								// Start to handle the input
								gb_userPipeToSuccess =false;
								gb_userPipeFromSuccess =false;

								hasNumberPipe = false;
								bothStderr = false;
								pipeAfterLine =0 ;
	
								hasUserPipeFrom = false;
								hasUserPipeTo =false ;
								userPipeFrom =-1;
								userPipeTo =-1;

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
									// broadcast to everyone that you left
									// just leave -> ipcSocket will be closed -> Parent will know we leave and handle logoutMessage.
									// the only thing need to do is to close all the gb_openRd
									for(int k=0;k<MAX_USERNUMBER;k++){
										if(gb_openReadFd[k]!= -1){
											close(gb_openReadFd[k]);
											gb_openReadFd[k] =-1;
										}
									}
									break ;
								}else if(commandVec.size()!=0 && commandVec[0]=="printenv"){
									if(commandVec.size()==2){
										callPrintenv(commandVec[1],slaveSocket);     
									} 
								}else if(commandVec.size()!=0 && commandVec[0]=="setenv"){
									if(commandVec.size()==3){
										callSetenv(commandVec[1],commandVec[2]);    
									}
								}else if(commandVec.size()!=0 && commandVec[0]=="name"){
									// client want to change name
									// send client input to user	
									string sendMessage = commandVec[0]+" "+commandVec[1];
									write(ipcSocket,sendMessage.c_str(),sendMessage.length());
									// wait until signal
									pause();

								}else if(commandVec.size()!=0 && commandVec[0]=="tell"){
									string sendMessage = commandVec[0] + " " + commandVec[1] +" "+ input.substr(input.find(commandVec[1])+commandVec[1].length()+1);
									write(ipcSocket,sendMessage.c_str(),sendMessage.length());
									pause(); //just testing

								}else if(commandVec.size()!=0 && commandVec[0]=="yell"){
									string sendMessage = commandVec[0] ;
									sendMessage = sendMessage + " " + input.substr(input.find("yell")+5);
									write(ipcSocket,sendMessage.c_str(),sendMessage.length());
									pause();

								}else if(commandVec.size()!=0 && commandVec[0]=="who"){
									string sendMessage = commandVec[0] ;
									write(ipcSocket,sendMessage.c_str(),sendMessage.length());
									pause();

								}else if(commandVec.size()!=0){ // The last condition is not empty.
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
										}else if(commandVec[i].find("<") != string::npos && commandVec[i].length() >1){
											// find <id userPipeFrom
											hasUserPipeFrom = true;
											userPipeFrom = stoi(commandVec[i].substr(1));

										}else if(commandVec[i].find(">") != string::npos && commandVec[i].length() >1){
											// find >id userPipeTo
											hasUserPipeTo =true ;
											userPipeTo = stoi(commandVec[i].substr(1));

										}
									} 
									
									// judge userPipeFrom & userPipeTo
									// Ask Parent to check if this command is legal or not.
									if(hasUserPipeFrom == true){
										string sendMessage = "<"+to_string(userPipeFrom)+" "+input ; 
										write(ipcSocket,sendMessage.c_str(),sendMessage.length());	
										pause();
									}
									if(hasUserPipeTo == true){
										string sendMessage = ">"+to_string(userPipeTo)+" "+input ;
										write(ipcSocket,sendMessage.c_str(),sendMessage.length());	
										pause();
									}

									// Now we have the number of processes 
									// we can start to construct the pipe.
									if(process_count ==1){
										singleProcess(commandVec,hasNumberPipe,bothStderr,pipeAfterLine,numberPipe,pipe_expired_table,slaveSocket,hasUserPipeFrom,hasUserPipeTo,userPipeFrom);	
									}else if(process_count>=2){
										multiProcess(commandVec,process_count,hasNumberPipe,bothStderr,pipeAfterLine,numberPipe,pipe_expired_table,slaveSocket,hasUserPipeFrom,hasUserPipeTo,userPipeFrom);	
									}		
								}
					
								//one term command is done -> Initialize it.
								commandVec.clear();
								ss.str("");
								ss.clear();
								write(slaveSocket,promptBuffer,2);
							}
						}
						// client had left.
						userlist[emptyIndex].reset();
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
						cout << "*** readCount == -1 / might be interrupted by signal ***\n";
						cout << "*** handle it -> read again ***\n";
					}
					if(readCount ==0){
						// readCount ==0 means the process might dead.
						cout <<userlist[existUserIndex[i]].getId()<<"\'s process dead\n"; 	

						// broadcast to everyone userlist[existUserIndex[i]] left ,Except user who send this message ;
						string broadcastMessage = "secretcode_d"+to_string(userlist[existUserIndex[i]].getId())+"*** User \'"+userlist[existUserIndex[i]].getName()+ "\' left. ***\n";
						vector<int> broadcastUserIndex = existUser(userlist);
						for(int n=0; n<broadcastUserIndex.size() ; n++){
							if(existUserIndex[i] != broadcastUserIndex[n]){ // except who send this message
								write(userlist[broadcastUserIndex[n]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());
								userlist[broadcastUserIndex[n]].setUserPipeFd(userlist[existUserIndex[i]].getId(),-1);
								kill(userlist[broadcastUserIndex[n]].getPid(),SIGUSR1);
							}
						}

						FD_CLR(userlist[existUserIndex[i]].getIpcSocketfd(),&afds);
						close(userlist[existUserIndex[i]].getIpcSocketfd());
						userlist[existUserIndex[i]].reset();

					}else{
						// readCount >0 this process wanna say something
						// in this process there are 4 kinds of command need it OR broadcast login logout .
						// who / name / tell / yell 
						input = extractClientInput(buffer,readCount);
						cout << userlist[existUserIndex[i]].getId() <<"\'s Process sends \'" <<input << "\' to Parent\n";
						// develop - who name tell yell AND broadcast login logout 
						
						if(input == "login"){			
							// broadcast to everyone userlist[existUserIndex[i]] login ;
							string broadcastMessage = "*** User \'"+ userlist[existUserIndex[i]].getName() + "\' entered from "+ userlist[existUserIndex[i]].getIpAddress()+":" +to_string(userlist[existUserIndex[i]].getPort())+". ***\n";
							vector<int> broadcastUserIndex = existUser(userlist);
							for(int n=0; n<broadcastUserIndex.size() ; n++){
								write(userlist[broadcastUserIndex[n]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());
								kill(userlist[broadcastUserIndex[n]].getPid(),SIGUSR1);
							}

						}else if(input.substr(0,4)=="name"){
							string newName = input.substr(5); // name xyzabc -> substr(5) = xyzabc
							string broadcastMessage ="";
							bool noSameName = "true"; 

							// check if there is same name
							vector<int> broadcastUserIndex = existUser(userlist);
							for(int k=0;k<broadcastUserIndex.size();k++){
								if(userlist[broadcastUserIndex[k]].getName() == newName){		
									noSameName = false;
									break;
								}				
							}

							if(noSameName ==true){
								// reset this client's name and broadcast
								userlist[existUserIndex[i]].setName(newName);
								broadcastMessage = "*** User from " + userlist[existUserIndex[i]].getIpAddress()+":"+to_string(userlist[existUserIndex[i]].getPort())+ " is named \'" + userlist[existUserIndex[i]].getName()+"\'. ***\n" ;

								for(int n=0; n<broadcastUserIndex.size() ; n++){
									write(userlist[broadcastUserIndex[n]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());
									kill(userlist[broadcastUserIndex[n]].getPid(),SIGUSR1);
								}

							}else if(noSameName ==false){
								// noSameName == false -> just send error message to client who want to rename 
								broadcastMessage = "*** User \'" + newName + "\' already exists. ***\n" ;
								write(userlist[existUserIndex[i]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());
								kill(userlist[existUserIndex[i]].getPid(),SIGUSR1);
							}

						}else if(input.substr(0,4) == "tell"){
							string target = "";
							string message = "";
							int spaceIndex =-1;
							string broadcastMessage ="" ;
							bool isUserOnline = false;

							for(int n=5;n<input.length();n++){
								if(input[n] == ' '){
									spaceIndex = n;
									break;
								}	
							}
							
							target = input.substr(5,spaceIndex-5);
							message = input.substr(spaceIndex+1);
							
							// Find whether user online or not
							cout << "target: " << target << " message: " << message <<"\n";
							isUserOnline = userlist[stoi(target)-1].getAvailable();

							if(isUserOnline ==true){
								broadcastMessage = "*** "+ userlist[existUserIndex[i]].getName() + " told you ***: " + message + "\n";
								write(userlist[stoi(target)-1].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());
								kill(userlist[stoi(target)-1].getPid(),SIGUSR1);
								
								string secret = "secretcode_ignore";
								write(userlist[existUserIndex[i]].getIpcSocketfd(),secret.c_str(),secret.length());
								kill(userlist[existUserIndex[i]].getPid(),SIGUSR1);

							}else{
								broadcastMessage ="*** Error: user #"+ target +" does not exist yet. ***\n";
								write(userlist[existUserIndex[i]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());
								kill(userlist[existUserIndex[i]].getPid(),SIGUSR1);

							}
							
							
						}else if(input.substr(0,4) == "yell"){
							string broadcastMessage = "*** "+userlist[existUserIndex[i]].getName()+" yelled ***: "+input.substr(5) +"\n";
							vector<int> broadcastUserIndex = existUser(userlist);
							for(int n=0;n<broadcastUserIndex.size();n++){
								write(userlist[broadcastUserIndex[n]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());
								kill(userlist[broadcastUserIndex[n]].getPid(),SIGUSR1);
							}

						}else if(input.substr(0,3)== "who"){
							string broadcastMessage = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";	
							
							vector<int> broadcastUserIndex = existUser(userlist);
							for(int n=0;n<broadcastUserIndex.size();n++){
								broadcastMessage += to_string(userlist[broadcastUserIndex[n]].getId())+"\t"+userlist[broadcastUserIndex[n]].getName()+"\t"+userlist[broadcastUserIndex[n]].getIpAddress()+":"+to_string(userlist[broadcastUserIndex[n]].getPort()) ;
								if(userlist[broadcastUserIndex[n]].getId() == userlist[existUserIndex[i]].getId()){
									broadcastMessage += "\t<-me\n";
								}else{
									broadcastMessage += "\n";	
								}
							}			
							cout << broadcastMessage ;
							write(userlist[existUserIndex[i]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());
							kill(userlist[existUserIndex[i]].getPid(),SIGUSR1);
						}else if(input[0] == '<' && input.length() >1){
							// get Ask from child process -> userPipeFrom 
							hasUserPipeFrom = true;
							int spaceIndex = -1;
							for(int k=0;k<input.length();k++){
								if(input[k] ==' '){
									spaceIndex = k ;
									break;
								}
							}
							userPipeFrom = stoi(input.substr(1,spaceIndex-1));
							string broadcastMessage = "";

							// check if this command is legal
							if(userPipeFrom >30 || userlist[userPipeFrom-1].getAvailable() ==false){
								// user does not exist
								broadcastMessage ="*** Error: user #" + to_string(userPipeFrom) + " does not exist yet. ***\n";
								write(userlist[existUserIndex[i]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());
								kill(userlist[existUserIndex[i]].getPid(),SIGUSR1);

							}else if(userPipeFrom <=30 && userlist[userPipeFrom-1].getAvailable() ==true && userlist[existUserIndex[i]].getUserPipeFd(userPipeFrom) ==-1){
								// userPipe does not exist yet.
								broadcastMessage ="*** Error: the pipe #"+to_string(userPipeFrom)+"->#"+to_string(userlist[existUserIndex[i]].getId())+" does not exist yet. ***\n";
								write(userlist[existUserIndex[i]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());
								kill(userlist[existUserIndex[i]].getPid(),SIGUSR1);

							}else{
								// success -> parent need to record the information
								// developHEAD	
								userlist[existUserIndex[i]].setUserPipeFd(userPipeFrom,-1);

								// success -> broadcast to everyone
								vector<int> broadcastUserIndex = existUser(userlist);
								for(int n=0;n<broadcastUserIndex.size();n++){
									if(broadcastUserIndex[n] == existUserIndex[i]){
										// userlist[existUserIndex[i]] need to receive from FIFO -> use another signal
										broadcastMessage= "secretcode_r="+ to_string(userPipeFrom)+"_"+to_string(userlist[existUserIndex[i]].getId()) +"*** "+userlist[existUserIndex[i]].getName()+" (#"+to_string(userlist[existUserIndex[i]].getId())+") just received from "+userlist[userPipeFrom-1].getName()+" (#"+to_string(userPipeFrom)+") by \'"+input.substr(spaceIndex+1)+"\' ***\n";
										write(userlist[broadcastUserIndex[n]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());	
										kill(userlist[existUserIndex[i]].getPid(),SIGUSR2);
									}else{
										// other people just receive the message and show it to client
										broadcastMessage= "*** "+userlist[existUserIndex[i]].getName()+" (#"+to_string(userlist[existUserIndex[i]].getId())+") just received from "+userlist[userPipeFrom-1].getName()+" (#"+to_string(userPipeFrom)+") by \'"+input.substr(spaceIndex+1)+"\' ***\n";
										write(userlist[broadcastUserIndex[n]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());	
										kill(userlist[broadcastUserIndex[n]].getPid(),SIGUSR1);
									}
								}

							}

						}else if(input[0] == '>' && input.length() >1){
							// get Ask from child process -> userPipeTo
							hasUserPipeTo = true;
							int spaceIndex = -1;
							for(int k=0;k<input.length();k++){
								if(input[k] ==' '){
									spaceIndex = k ;
									break;
								}
							}
							userPipeTo = stoi(input.substr(1,spaceIndex-1));
							string broadcastMessage = "";

							// check if this command is legal
							if(userPipeTo >30 || userlist[userPipeTo-1].getAvailable() ==false){
								// user does not exist
								broadcastMessage ="*** Error: user #" + to_string(userPipeTo) + " does not exist yet. ***\n";
								write(userlist[existUserIndex[i]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());
								kill(userlist[existUserIndex[i]].getPid(),SIGUSR1);

							}else if(userPipeTo <=30 && userlist[userPipeTo-1].getAvailable() ==true && userlist[userPipeTo-1].getUserPipeFd(userlist[existUserIndex[i]].getId()) != -1){
								// userPipe already exist yet.
								broadcastMessage ="*** Error: the pipe #"+to_string(userlist[existUserIndex[i]].getId())+"->#"+to_string(userPipeTo)+" already exists. ***\n";
								write(userlist[existUserIndex[i]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());
								kill(userlist[existUserIndex[i]].getPid(),SIGUSR1);

							}else{
								// success -> parent need to record the information
								// developHEAD
								userlist[userPipeTo-1].setUserPipeFd(userlist[existUserIndex[i]].getId(),1);	

								// success -> broadcast to everyone
								vector<int> broadcastUserIndex = existUser(userlist);
								for(int n=0;n<broadcastUserIndex.size();n++){
									if(broadcastUserIndex[n] == existUserIndex[i]){
										// userlist[existUserIndex[i]] need to write to FIFO -> use another signal
										broadcastMessage= "secretcode_w="+to_string(userlist[existUserIndex[i]].getId())+"_"+to_string(userlist[userPipeTo-1].getId())+"*** "+userlist[existUserIndex[i]].getName()+" (#"+to_string(userlist[existUserIndex[i]].getId())+") just piped \'"+input.substr(spaceIndex+1)+"\' to "+userlist[userPipeTo-1].getName()+" (#"+to_string(userPipeTo)+") ***\n";
										write(userlist[broadcastUserIndex[n]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());	
										kill(userlist[existUserIndex[i]].getPid(),SIGUSR2);

									}else if(broadcastUserIndex[n] == (userPipeTo-1) ){
										// also need to remind read side
										broadcastMessage= "secretcode_o="+to_string(userlist[existUserIndex[i]].getId())+"_"+to_string(userlist[userPipeTo-1].getId())+"*** "+userlist[existUserIndex[i]].getName()+" (#"+to_string(userlist[existUserIndex[i]].getId())+") just piped \'"+input.substr(spaceIndex+1)+"\' to "+userlist[userPipeTo-1].getName()+" (#"+to_string(userPipeTo)+") ***\n";
										write(userlist[broadcastUserIndex[n]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());	
										kill(userlist[broadcastUserIndex[n]].getPid(),SIGUSR2);

									}else{
										// other people just receive the message and show it to client
										broadcastMessage= "*** "+userlist[existUserIndex[i]].getName()+" (#"+to_string(userlist[existUserIndex[i]].getId())+") just piped \'"+input.substr(spaceIndex+1)+"\' to "+userlist[userPipeTo-1].getName()+" (#"+to_string(userPipeTo)+") ***\n";
										write(userlist[broadcastUserIndex[n]].getIpcSocketfd(),broadcastMessage.c_str(),broadcastMessage.length());	
										kill(userlist[broadcastUserIndex[n]].getPid(),SIGUSR1);

									}
								}
							}
						}
					}
				}
			}
		}
	}
	close(masterSocket);
	return 0;
}
// Single process handle 
void singleProcess(vector<string> commandVec,bool hasNumberPipe,bool bothStderr,int pipeAfterLine,int numberPipe[PET_SIZE][2],int pipe_expired_table[PET_SIZE],int slaveSocket,bool hasUserPipeFrom,bool hasUserPipeTo,int userPipeFrom){
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
			if((i+1)<commandVec.size() && commandVec[i].length()==1){
				needRedirection = true ;
				redirectionFileName = commandVec[i+1] ;
				break;

			}else if(commandVec[i].length() ==1 && (i+1)== commandVec.size()){
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
		int arg_index =0;
        for(int i=0;i<commandVec.size();i++){
			if(commandVec[i].find(">")==string::npos && commandVec[i].find("|")==string::npos && commandVec[i].find("!")==string::npos && commandVec[i].find("<")==string::npos){
        		arg[arg_index] = strdup(commandVec[i].c_str());
				arg_index ++ ;
			}else if((commandVec[i].find(">")!=string::npos && commandVec[i].length()>1) || (commandVec[i].find("<")!=string::npos && commandVec[i].length()>1) ){
				continue ;	
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
		}else if(hasUserPipeFrom ==true){
			if(gb_userPipeFromSuccess ==true){
				dup2(gb_openReadFd[userPipeFrom-1],STDIN_FILENO);	
				close(gb_openReadFd[userPipeFrom-1]);
				gb_openReadFd[userPipeFrom-1] =-1;

			}else{	
				int devnull = open("/dev/null",O_RDWR);
				dup2(devnull,STDIN_FILENO);
				close(devnull);
			}
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

		}else if(hasUserPipeTo ==true){
			// dup socket to STDERR_FILNO
			dup2(slaveSocket,STDERR_FILENO);
			close(slaveSocket);
			
			if(gb_userPipeToSuccess ==true){
				dup2(gb_openWriteFd,STDOUT_FILENO);
				close(gb_openWriteFd);

			}else{
				int devnull = open("/dev/null",O_RDWR);
				dup2(devnull,STDOUT_FILENO);
				close(devnull);
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
		
		//Before execvp -> Child closes useless userPipeFrom
		for(int i=0;i<MAX_USERNUMBER;i++){
			if(gb_openReadFd[i] !=-1){
				close(gb_openReadFd[i]);
				gb_openReadFd[i] =-1;
			}
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
		}else if(hasUserPipeFrom ==true && gb_userPipeFromSuccess ==true){
			// Parent need tidy userPipeFrom which is called
			close(gb_openReadFd[userPipeFrom-1]);
			gb_openReadFd[userPipeFrom-1] =-1;
		}
		
		// Parent need tidy userPipeTo
		if(hasUserPipeTo == true && gb_userPipeToSuccess ==true){
			close(gb_openWriteFd);			
		}

		// If this command has number Pipe
		// Parent doesn't need to wait child DONE.
		// otherwise need to wait.
		if(hasNumberPipe == true || hasUserPipeTo ==true){
			signal(SIGCHLD,wait4children);
		}else{
			waitpid(fork_pid,NULL,0);
		}
	}

}

// Multi process handle
void multiProcess(vector<string>commandVec,int process_count,bool hasNumberPipe,bool bothStderr,int pipeAfterLine,int numberPipe[PET_SIZE][2],int pipe_expired_table[PET_SIZE],int slaveSocket,bool hasUserPipeFrom,bool hasUserPipeTo,int userPipeFrom){
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
		}else if(commandVec[cmd_index].find("<")!= string::npos){
			continue;

		}else if(commandVec[cmd_index].find(">")!= string::npos){ //Find > in this string
			if((cmd_index+1) < commandVec.size() && commandVec[cmd_index].length() ==1){ 
				needRedirection = true ;
				redirectionFileName = commandVec[cmd_index+1];
				break ;

			}else if(commandVec[cmd_index].length() >1){
				continue ;

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

			}else if(hasUserPipeFrom ==true){		
				if(gb_userPipeFromSuccess ==true){
					dup2(gb_openReadFd[userPipeFrom-1],STDIN_FILENO);	
					close(gb_openReadFd[userPipeFrom-1]);
					gb_openReadFd[userPipeFrom-1] =-1;

				}else{	
					int devnull = open("/dev/null",O_RDWR);
					dup2(devnull,STDIN_FILENO);
					close(devnull);
				}
			}		

			// Child1 need to modify the stdout
			close(mPipe[0][0]);
			dup2(mPipe[0][1],STDOUT_FILENO);
			close(mPipe[0][1]);
			
			// Child 1 doesn't need slaveSocket
			// But STDERR may need it so -> dup to STDERR
			dup2(slaveSocket,STDERR_FILENO);
			close(slaveSocket);

			//Before execvp -> Child closes useless userPipeFrom
			for(int i=0;i<MAX_USERNUMBER;i++){
				if(gb_openReadFd[i] !=-1){
					close(gb_openReadFd[i]);
					gb_openReadFd[i] =-1;
				}
			}

			// since first child doesn't need userPipeTo
			if(hasUserPipeTo ==true && gb_userPipeToSuccess ==true){
				if(gb_openWriteFd != -1){
					close(gb_openWriteFd);
				}			
			}

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
			}else if(hasUserPipeFrom ==true && gb_userPipeFromSuccess ==true){		
				// Parent need tidy userPipeFrom which is called
				close(gb_openReadFd[userPipeFrom-1]);
				gb_openReadFd[userPipeFrom-1] =-1;
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

				}else if(hasUserPipeTo ==true){			
					// dup socket to STDERR_FILNO
					dup2(slaveSocket,STDERR_FILENO);
					close(slaveSocket);
					
					if(gb_userPipeToSuccess ==true){
						dup2(gb_openWriteFd,STDOUT_FILENO);
						close(gb_openWriteFd);

					}else{
						int devnull = open("/dev/null",O_RDWR);
						dup2(devnull,STDOUT_FILENO);
						close(devnull);
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
				
				//Before execvp -> Child closes useless userPipeFrom
				for(int i=0;i<MAX_USERNUMBER;i++){
					if(gb_openReadFd[i] !=-1){
						close(gb_openReadFd[i]);
						gb_openReadFd[i] =-1;
					}
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

				// Parent need tidy userPipeTo
				if(hasUserPipeTo == true && gb_userPipeToSuccess ==true){
					close(gb_openWriteFd);			
					gb_openWriteFd =-1;
				}

				if(hasNumberPipe == true || hasUserPipeTo ==true){
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

			}else if(hasUserPipeFrom ==true){		
				if(gb_userPipeFromSuccess ==true){
					dup2(gb_openReadFd[userPipeFrom-1],STDIN_FILENO);	
					close(gb_openReadFd[userPipeFrom-1]);
					gb_openReadFd[userPipeFrom-1] =-1;

				}else{	
					int devnull = open("/dev/null",O_RDWR);
					dup2(devnull,STDIN_FILENO);
					close(devnull);
				}
			}

			// Child1 need to modify the stdout
			close(mPipe[0][0]);
			dup2(mPipe[0][1],STDOUT_FILENO);
			close(mPipe[0][1]);
			
			// Child1 doesn't need slaveSocket but STDERR may need
			dup2(slaveSocket,STDERR_FILENO);
			close(slaveSocket);

			//Before execvp -> Child closes useless userPipeFrom
			for(int i=0;i<MAX_USERNUMBER;i++){
				if(gb_openReadFd[i] !=-1){
					close(gb_openReadFd[i]);
					gb_openReadFd[i] =-1;
				}
			}
			// Before execvp -> close useless number Pipe
			for(int i=0;i<existPipeIndex.size();i++){
				close(numberPipe[existPipeIndex[i]][0]);
				close(numberPipe[existPipeIndex[i]][1]);
			}

			// since first child doesn't need userPipeTo
			if(hasUserPipeTo ==true && gb_userPipeToSuccess ==true){
				if(gb_openWriteFd != -1){
					close(gb_openWriteFd);
				}			
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
			}else if(hasUserPipeFrom ==true && gb_userPipeFromSuccess ==true){
				// Parent need tidy userPipeFrom which is called
				close(gb_openReadFd[userPipeFrom-1]);
				gb_openReadFd[userPipeFrom-1] =-1;
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

					//Before execvp -> Child closes useless userPipeFrom
					for(int i=0;i<MAX_USERNUMBER;i++){
						if(gb_openReadFd[i] !=-1){
							close(gb_openReadFd[i]);
							gb_openReadFd[i] =-1;
						}
					}
					// Before execvp -> close useless number pipe
					for(int i=0;i<existPipeIndex.size();i++){
						close(numberPipe[existPipeIndex[i]][0]);
						close(numberPipe[existPipeIndex[i]][1]);
					}

					// since middle child doesn't need userPipeTo
					if(hasUserPipeTo ==true && gb_userPipeToSuccess ==true){
						if(gb_openWriteFd != -1){
							close(gb_openWriteFd);
						}			
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
					
				}else if(hasUserPipeTo ==true){
					// dup socket to STDERR_FILNO
					dup2(slaveSocket,STDERR_FILENO);
					close(slaveSocket);
					
					if(gb_userPipeToSuccess ==true){
						dup2(gb_openWriteFd,STDOUT_FILENO);
						close(gb_openWriteFd);

					}else{
						int devnull = open("/dev/null",O_RDWR);
						dup2(devnull,STDOUT_FILENO);
						close(devnull);
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

				//Before execvp -> Child closes useless userPipeFrom
				for(int i=0;i<MAX_USERNUMBER;i++){
					if(gb_openReadFd[i] !=-1){
						close(gb_openReadFd[i]);
						gb_openReadFd[i] =-1;
					}
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

				// Parent need tidy userPipeTo
				if(hasUserPipeTo == true && gb_userPipeToSuccess ==true){
					close(gb_openWriteFd);			
					gb_openWriteFd =-1;
				}

				// Last process we can use wait -> SHELL will stop here
				// Until the last process done
				if(hasNumberPipe ==true || hasUserPipeTo ==true){
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

string extractClientInput(char buffer[MAX_LENGTH],int readCount){
	string result ="" ;
	
	for(int i=0;i<readCount;i++){
		if( ( buffer[i]=='\n' && (i+1)<readCount && (buffer[i+1]=='\r' || buffer[i+1]=='\0') ) || (buffer[i]=='\n' && (i+1)==readCount)  || buffer[i]=='\r' || buffer[i]=='\0'){
			break ;
		}
		result += buffer[i] ;
	}
	return result; 
}

// signal handler
void wait4children(int signo){
	int status;
	while(waitpid(-1,&status,WNOHANG)>0);
}

void sig_usr(int signo){
	if(signo == SIGUSR1){
		cout << "sig_usr: SIGUSR1\n";
		// DEFINE : receive from Parent and echo to client
		char buffer[MAX_LENGTH] = {};	
		string input = "";
		bool someoneDead =false;
		int deadId =-1;

		// receive message from Parent
		int readCount = read(gb_ipcSocket,buffer,sizeof(buffer));
		input = extractClientInput(buffer,readCount);
		
		// Dead check
		if(input.substr(0,12)=="secretcode_d" && input.find("left")!=string::npos){
			someoneDead =true;
			for(int k=12;k<input.length();k++){
				if(input[k] =='*'){
					deadId = stoi(input.substr(12,k-12));
					input = input.substr(k);
					break;
				}
			}
		}
		
		input += "\n";
		
		// echo to client
		if(someoneDead ==true){
			// clear this userPipe
			if(gb_openReadFd[deadId-1] != -1){
				close(gb_openReadFd[deadId-1]);
				gb_openReadFd[deadId-1] = -1;
			}
		}

		if(input.substr(0,input.length()-1) != "secretcode_ignore"){
			write(gb_slaveSocket,input.c_str(),input.length());
		}

	}else if(signo == SIGUSR2){
		// use for user Pipe handling
		// first also need to receive message from Parent and echo to client
		cout << "sig_usr: SIGUSR2\n";
		char buffer[MAX_LENGTH] = {};
		string input ="";
		string secretCommand ="";
		string pathName ="";
		int userPipeFrom = -1;	
		int readCount = read(gb_ipcSocket,buffer,sizeof(buffer));
		
		input = extractClientInput(buffer,readCount);
		secretCommand = input.substr(0,12);
		pathName = "./user_pipe/" + input.substr( input.find("=")+1,input.find("*")-input.find("=")-1);
		userPipeFrom = stoi( input.substr( input.find("=")+1 , input.find("_",12)-input.find("=")-1 ) );
		input = input.substr(input.find("*"));
		input +="\n";
		// echo to client
		write(gb_slaveSocket,input.c_str(),input.length());

		// Now start to handle FIFO -> only child process will execute this function
		// First check we are FIFO write end / FIFO read end -> via ipcMessage
		if(secretCommand == "secretcode_w"){
			gb_userPipeToSuccess =true ;
			// need to mkfifo & open (also need to some one to open read) 
			if(mkfifo(pathName.c_str(),0777)<0 && errno != EEXIST ){
				cout << "Child - secretcode_w can't create:" << pathName <<".\n";
			}
			cout << "Child - secretcode_w bf open with pathName: " << pathName << ".\n";
			gb_openWriteFd = open(pathName.c_str(),O_WRONLY,0);
			cout << "gb_openWriteFd=" << gb_openWriteFd << " and errno: " << errno <<"\n";
			cout << "Child - secretcode_w gb_openWriteFd not hanging!\n";

		}else if(secretCommand == "secretcode_o"){
			// need to help write side of the same file		
			if(mkfifo(pathName.c_str(),0777)<0 && errno != EEXIST ){
				cout << "Child - secretcode_o can't create:" << pathName <<".\n";
			}
			cout << "Child - secretcode_o bf open with userPipeFrom: " << userPipeFrom <<" and pathName: " << pathName << ".\n";
			gb_openReadFd[userPipeFrom-1] = open(pathName.c_str(),O_RDONLY,0);
			cout << "Child - secretcode_o & gb_openReadFd[userPipeFrom-1]=" << gb_openReadFd[userPipeFrom-1]<<" and errno: " << errno<< "\n";

		}else if(secretCommand == "secretcode_r"){
			gb_userPipeFromSuccess =true ;
		}		
	}
}
