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
#include <map>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/select.h>

#define PET_SIZE 1000
#define MAX_LENGTH 15000
#define SSOCKET_TABLE_SIZE 30
using namespace std;

void PET_init(int pipe_expired_table[PET_SIZE]);
void PET_iterate(int pipe_expired_table[PET_SIZE]); 
int PET_findExpired(int pipe_expired_table[PET_SIZE]);
int PET_findSameLine(int pipe_expired_table[PET_SIZE],int target_line);
int PET_emptyPipeIndex(int pipe_expired_table[PET_SIZE]);
vector<int> PET_existPipe(int pipe_expired_table[PET_SIZE]);

// User class
class User{
private:
	bool available ;

public:
	int numberPipe[PET_SIZE][2];
	int pipe_expired_table[PET_SIZE];
	int userPipe[SSOCKET_TABLE_SIZE][2];

	int socketfd ;
	int id ;
	string name ;
	string ipAddress ;
	int port ;
	map<string,string> envMap;
	

	// functions
	User(){
		PET_init(pipe_expired_table);
		userPipeInit();
		reset();
	}
	void init(int id,string ipAddress,int port){
		available = true;
		this -> id = id;
		this -> ipAddress = ipAddress;
		this -> port = port ;
		name = "(no name)" ;
		envMap["PATH"] = "bin:.";
		PET_init(pipe_expired_table);
	}
	void reset(){
		available = false;
		socketfd =-1;
		id =-1;
		name ="";
		ipAddress ="";
		port = -1;
		envMap.clear();

		for(int i=0;i<PET_SIZE;i++){
			if(pipe_expired_table[i]!= -1){ // Except empty:-1 
				close(numberPipe[i][0]);
				close(numberPipe[i][1]);
			}
		}
		PET_init(pipe_expired_table);

		// close all userPipe that isn't -1
		for(int i=0;i<SSOCKET_TABLE_SIZE;i++){
			if(userPipe[i][0] != -1){ // userPipe read exist -> close it.
				close(userPipe[i][0]);
			}
			if(userPipe[i][1] != -1){ // userPipe write exist -> close it.
				close(userPipe[i][1]);
			}
		}
		// who user exit -> also need to clear other user's pipe that I pass to
		

		// then set all fd to -1;
		userPipeInit();
	}
	void userPipeInit(){ // set all userPipe read write fd to -1
		for(int i=0;i<SSOCKET_TABLE_SIZE;i++){
			userPipe[i][0] =-1;
			userPipe[i][1] =-1;
		}		
	}
	void userPipeReset(int target){ // set target read write fd to -1 
		userPipe[target][0] =-1;
		userPipe[target][1] =-1;
	}
	
	void addMap(string key,string value){
		envMap[key]=value;
	}

	bool getUserPipeExist(int id){
		if(userPipe[id-1][0] != -1 && userPipe[id-1][1] != -1){
			return true;
		}else{
			return false;
		}
	}

	bool getUserPipeReadExist(int id){
		if(userPipe[id-1][0] != -1){
			return true;
		}else{
			return false;
		}
	}
	
	bool getUserPipeWriteExist(int id){
		if(userPipe[id-1][1] != -1){
			return true;
		}else{
			return false;
		}
	}
	
	bool getAvailable(){
		return available;
	}

};

void callPrintenv(string envVar,User user);
void callSetenv(string envVar,string value);
void singleProcess(vector<string>commandVec,bool hasNumberPipe,bool bothStderr,int pipeAfterLine,int numberPipe[PET_SIZE][2],int pipe_expired_table[PET_SIZE],User userlist[SSOCKET_TABLE_SIZE],User* user,bool hasUserPipeFrom,bool hasUserPipeTo,int userPipeFrom,int userPipeTo,string input);
void multiProcess(vector<string>commandVec,int process_count,bool hasNumberPipe,bool bothStderr,int pipeAfterLine,int numberPipe[PET_SIZE][2],int pipe_expired_table[PET_SIZE],User userlist[SSOCKET_TABLE_SIZE],User* user,bool hasUserPipeFrom,bool hasUserPipeTo,int userPipeFrom,int userPipeTo,string input);

void wait4children(int signo);

string extractClientInput(char buffer[MAX_LENGTH],int readCount);

void clearMessageUserPass(User logoutUser,User userlist[SSOCKET_TABLE_SIZE]);

void dbug(int line);


int users_findEmpty(User users[SSOCKET_TABLE_SIZE]);
vector<int> users_exist(User users[SSOCKET_TABLE_SIZE]);


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
	bool hasUserPipeFrom = false;
	bool hasUserPipeTo = false;
	int userPipeFrom =0;
	int userPipeTo =0;

	// Socket setting
	int masterSocket,clientLen,readCount ;
	struct sockaddr_in clientAddr , serverAddr;
	char buffer[MAX_LENGTH] ={} ;
	string promptString ="% ";
	bool bReuseAddr= true;
	int port = stoi(argv[1]); 
	User users[SSOCKET_TABLE_SIZE] ;

	// select setting
	int rtnVal ;
	fd_set rfds,afds ;
	int nfdp = getdtablesize();
	FD_ZERO(&rfds);
	FD_ZERO(&afds);	
	
	// Master socket setting 
	if((masterSocket = socket(AF_INET,SOCK_STREAM,0))<0){
		cerr << "master socket create Fail\n";
		exit(0);
	}else{
		// We add master Socket to afds
		FD_SET(masterSocket,&afds);
	}
	bzero(&serverAddr,sizeof(serverAddr));
	serverAddr.sin_family = AF_INET ;
	serverAddr.sin_addr.s_addr =htonl(INADDR_ANY);
	serverAddr.sin_port = htons(port);
	
	setsockopt(masterSocket,SOL_SOCKET,SO_REUSEADDR,(const char*)&bReuseAddr,sizeof(bool));

	if(bind(masterSocket,(struct sockaddr *)&serverAddr,sizeof(serverAddr))<0){
		cerr <<"master socket bind Fail\n" ;
		exit(0);
	}
	cout <<"Server is listening...\n";
	listen(masterSocket,50);

	// set $PATH to bin/ ./ initially
	callSetenv("PATH","bin:.");

	while(1){
		memcpy(&rfds,&afds,sizeof(rfds));
		rtnVal = select(nfdp,&rfds,(fd_set *)0,(fd_set *)0,(struct timeval *)0);
		if(rtnVal <0){
			continue ;
		}else if(rtnVal >0){
			// rntVal > 0 We need to check which socket need to transfer data.
			// first check master Socket
			if(FD_ISSET(masterSocket,&rfds)){
				//  client want to connect to Server
				int emptyIndex = users_findEmpty(users) ;
				clientLen = sizeof(clientAddr);
				users[emptyIndex].socketfd = accept(masterSocket,(struct sockaddr *)&clientAddr,(socklen_t *)&clientLen);

				if(users[emptyIndex].socketfd <0){
					cerr << "slaveSocket Accept error\n";
					users[emptyIndex].reset();
				}else{
					// users initialization 
					users[emptyIndex].init((emptyIndex+1),string(inet_ntoa(clientAddr.sin_addr)),(int)ntohs(clientAddr.sin_port));
					// slave socket create Success -> add it to afds
					FD_SET(users[emptyIndex].socketfd,&afds);
					// send (1)Welcome (2)broadcast (3) prompt
					string welcome_1 = "****************************************\n";
					string welcome_2 = "** Welcome to the information server. **\n";
					write(users[emptyIndex].socketfd,welcome_1.c_str(),welcome_1.length());
					write(users[emptyIndex].socketfd,welcome_2.c_str(),welcome_2.length());
					write(users[emptyIndex].socketfd,welcome_1.c_str(),welcome_1.length());
					// broadcast
					string loginMessage = "*** User \'"+ users[emptyIndex].name + "\' entered from "+ users[emptyIndex].ipAddress+":" +to_string(users[emptyIndex].port)+". ***\n";
					cout << loginMessage ;

					vector<int> existUsersIndex = users_exist(users);
					for(int i=0;i<existUsersIndex.size();i++){
						write(users[existUsersIndex[i]].socketfd,loginMessage.c_str(),loginMessage.length());
					}
					// prompt
					write(users[emptyIndex].socketfd,promptString.c_str(),promptString.length());
				}
			}
			// Second check slave socket
			vector<int> existUsersIndex = users_exist(users);
			for(int i=0;i<existUsersIndex.size();i++){
				if(FD_ISSET(users[existUsersIndex[i]].socketfd,&rfds)){
					// get some message from slave Socket.
					readCount = read(users[existUsersIndex[i]].socketfd,buffer,sizeof(buffer));
					if(readCount ==0){
						// readCount ==0 means socket close. -> Handle it.
						FD_CLR(users[existUsersIndex[i]].socketfd,&afds);
						close(users[existUsersIndex[i]].socketfd);
						string tempName = users[existUsersIndex[i]].name;	
						clearMessageUserPass(users[existUsersIndex[i]],users);
						users[existUsersIndex[i]].reset();	

						// broadcast
						string logoutMessage = "*** User \'"+tempName+ "\' left. ***\n";

						vector<int> existUsersIndex = users_exist(users);
						for(int j=0;j<existUsersIndex.size();j++){
							write(users[existUsersIndex[j]].socketfd,logoutMessage.c_str(),logoutMessage.length());
						}
						cout << logoutMessage;
						
					}else{	
						input = extractClientInput(buffer,readCount);
						cout <<"User: "<< users[existUsersIndex[i]].name <<" ID: "<< users[existUsersIndex[i]].id <<" says: " << input <<"\n";
						// for each client -> reset environment variable first.
						clearenv();
						for(map<string,string>::iterator it=users[existUsersIndex[i]].envMap.begin();it!=users[existUsersIndex[i]].envMap.end();++it){
							callSetenv(it->first,it->second);
						}

						// Now we got input from client -> handle the message.
						// Flag initialization
						hasNumberPipe = false;
						bothStderr = false;
						pipeAfterLine =0 ;
						hasUserPipeFrom = false;
						hasUserPipeTo = false;
						userPipeFrom =0;
						userPipeTo =0;

						ss << input ;
				    	while (ss >> aWord) {
							commandVec.push_back(aWord);
						}
						// each round except for empty command  -> PET_iterate() 
						if(commandVec.size()!=0 && commandVec[0] != "exit"){
							PET_iterate(users[existUsersIndex[i]].pipe_expired_table);
						}
				
						// We want to check if the command is the three built-in command
						if(commandVec.size()!=0 && commandVec[0]=="exit"){
							// client want to exit ->  rm afds / close Socket / reset user.
							FD_CLR(users[existUsersIndex[i]].socketfd,&afds);
							close(users[existUsersIndex[i]].socketfd);
							string tempName = users[existUsersIndex[i]].name; 
							clearMessageUserPass(users[existUsersIndex[i]],users);
							users[existUsersIndex[i]].reset();
							// broadcast
							string logoutMessage = "*** User \'"+tempName+ "\' left. ***\n";

							vector<int> existUsersIndex = users_exist(users);
							for(int j=0;j<existUsersIndex.size();j++){
								write(users[existUsersIndex[j]].socketfd,logoutMessage.c_str(),logoutMessage.length());
							}
							cout << logoutMessage;

						}else if(commandVec.size()!=0 && commandVec[0]=="printenv"){
							if(commandVec.size()==2){
								callPrintenv(commandVec[1],users[existUsersIndex[i]]);     
							} 
						}else if(commandVec.size()!=0 && commandVec[0]=="setenv"){
							if(commandVec.size()==3){
								callSetenv(commandVec[1],commandVec[2]);    
								// add variable to user's map
								users[existUsersIndex[i]].envMap[commandVec[1]]=commandVec[2];
							}
						// 4 communication function 
						}else if(commandVec.size()!=0 && commandVec[0] == "who"){
							// this function is used to check who is in the system
							vector<int> newestUsersIndex = users_exist(users);
							int whoCallerID = users[existUsersIndex[i]].id ;

							// send message
							string whoMessage = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
							cout << whoMessage ;
							write(users[existUsersIndex[i]].socketfd,whoMessage.c_str(),whoMessage.length());
							for(int n=0;n<newestUsersIndex.size();n++){
									whoMessage = to_string(users[newestUsersIndex[n]].id) +"\t"+users[newestUsersIndex[n]].name+"\t"+users[newestUsersIndex[n]].ipAddress+":"+to_string(users[newestUsersIndex[n]].port);
									if(users[newestUsersIndex[n]].id != whoCallerID){ // it isn't me
										whoMessage += "\n";
									}else{ // it is me!
										whoMessage += "\t<-me\n";
									}
									cout << whoMessage;
									write(users[existUsersIndex[i]].socketfd,whoMessage.c_str(),whoMessage.length());
							}

						}else if(commandVec.size()!=0 && commandVec[0] == "name"){
							// this function is used to change user name
							vector<int> newestUsersIndex = users_exist(users);
							string allocateName = commandVec[1];
							bool hadSameName = false;
							string nameMessage ="" ;

							// check if there is sameName 
							for(int n=0;n<newestUsersIndex.size();n++){
								if(users[newestUsersIndex[n]].name == allocateName){
									// needless to broadcast since rename fail.
									hadSameName = true ;
									break ;
								}
							}

							if(hadSameName == false){
								// change name
								users[existUsersIndex[i]].name = allocateName ;
								nameMessage = "*** User from " + users[existUsersIndex[i]].ipAddress +":"+to_string(users[existUsersIndex[i]].port)+ " is named \'" + users[existUsersIndex[i]].name + "\'. ***\n" ;
								cout << nameMessage ;

								// broadcast
								for(int n=0;n<newestUsersIndex.size();n++){
									write(users[newestUsersIndex[n]].socketfd,nameMessage.c_str(),nameMessage.length());
								}

							}else if(hadSameName == true){
								// Only send fail message to the client who named.	
								nameMessage = "*** User \'" + allocateName + "\' already exists. ***\n" ;
								cout << nameMessage ;
								write(users[existUsersIndex[i]].socketfd,nameMessage.c_str(),nameMessage.length());
							}

						}else if(commandVec.size()!=0 && commandVec[0] == "tell"){
							// first use substr to handle tellMessage;
							// we need to know 'id' index in input.
							int idIndex = input.find(commandVec[1]);					
							string tellMessage = input.substr(idIndex+commandVec[1].length()+1);

							// tellMessage is handled
							// Now we check if receiver existed.
							vector<int> newestUsersIndex = users_exist(users);
							bool receiverExist = false;

							for(int n=0;n<newestUsersIndex.size();n++){
								if(users[newestUsersIndex[n]].id == stoi(commandVec[1])){
									// find receiver -> send message to it.
									receiverExist = true;
									tellMessage = "*** "+users[existUsersIndex[i]].name+" told you ***: "+tellMessage+"\n";
									cout << tellMessage ;
									write(users[newestUsersIndex[n]].socketfd,tellMessage.c_str(),tellMessage.length());

									break;
								}
							}
							
							if(receiverExist == false){
								// receiver doesn't exist -> print error message to sender
								tellMessage = "*** Error: user #" +commandVec[1]+" does not exist yet. ***\n";
								cout << tellMessage ;
								write(users[existUsersIndex[i]].socketfd,tellMessage.c_str(),tellMessage.length());
							}

						}else if(commandVec.size()!=0 && commandVec[0] == "yell"){
							// first use substr to get yellMessage.
							string yellMessage = input.substr(5);
							yellMessage = "*** "+users[existUsersIndex[i]].name +" yelled ***: " + yellMessage +"\n";

							// second get all user 
							vector<int> newestUsersIndex = users_exist(users);
							for(int n=0;n<newestUsersIndex.size();n++){
								write(users[newestUsersIndex[n]].socketfd,yellMessage.c_str(),yellMessage.length());	
							}
							cout << yellMessage ;

						}else if(commandVec.size()!=0){ // The last condition is not empty.
							// Not the three built-in command
							// Ready to handle the command. 
				
							// We want to know how much process need to call fork()
							// And check if there is number pipe -> flag on.
							// if there is userPipeFrom -> flag on.
							// if there is userPipeTo -> flag on.
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
								}else if(commandVec[i].find(">")!= string::npos && commandVec[i].length() >1){ 
									// find '>' and length >1 which means it has userPipeTo.
									hasUserPipeTo =true;
									userPipeTo = stoi(commandVec[i].substr(1));
									cout << "userPipeTo:" << userPipeTo <<"\n"; //dbg
								}else if(commandVec[i].find("<")!= string::npos && commandVec[i].length() >1){
									// find '<' and length >1 which means it has userPipeFrom.
									hasUserPipeFrom =true;
									userPipeFrom = stoi(commandVec[i].substr(1));
								}
							} 
							
							// Now we have the number of processes 
							// we can start to construct the pipe.
							if(process_count ==1){
								singleProcess(commandVec,hasNumberPipe,bothStderr,pipeAfterLine,users[existUsersIndex[i]].numberPipe,users[existUsersIndex[i]].pipe_expired_table,users,&users[existUsersIndex[i]],hasUserPipeFrom,hasUserPipeTo,userPipeFrom,userPipeTo,input);	
							}else if(process_count>=2){
								multiProcess(commandVec,process_count,hasNumberPipe,bothStderr,pipeAfterLine,users[existUsersIndex[i]].numberPipe,users[existUsersIndex[i]].pipe_expired_table,users,&users[existUsersIndex[i]],hasUserPipeFrom,hasUserPipeTo,userPipeFrom,userPipeTo,input);	
							}		
						}
				
						//one term command is done -> Initialize it.
						commandVec.clear();
						ss.str("");
						ss.clear();
						write(users[existUsersIndex[i]].socketfd,promptString.c_str(),promptString.length());
					}
				}
			}
		}
	} 
	return 0;
}
// Single process handle 
void singleProcess(vector<string> commandVec,bool hasNumberPipe,bool bothStderr,int pipeAfterLine,int numberPipe[PET_SIZE][2],int pipe_expired_table[PET_SIZE],User userlist[SSOCKET_TABLE_SIZE],User* user,bool hasUserPipeFrom,bool hasUserPipeTo,int userPipeFrom,int userPipeTo,string input){
	pid_t child_done_pid;
	int child_done_status;
	bool needRedirection =false;
	string redirectionFileName ="";
	char* arg[MAX_LENGTH];

	// If number pipe expired -> remember to handle it.
	bool userPipeToNewCreate = false;
	int newNumberPipeIndex;
	int pipeToSameLine;
	int expiredIndex = PET_findExpired(pipe_expired_table);	
	vector<int> existPipeIndex = PET_existPipe(pipe_expired_table); 	
	vector<int> existUsersIndex = users_exist(userlist);
	
	cout << "actually in single\n"; //dbg

	// handle broadcast at first time
	if(hasUserPipeFrom ==true && userPipeFrom <=30 && userlist[userPipeFrom-1].getAvailable()==true && user->getUserPipeExist(userPipeFrom)==true){
		// Broadcast
		string userPipeMessage = "*** "+user->name+" (#"+to_string(user->id)+") just received from "+userlist[userPipeFrom-1].name+" (#"+to_string(userPipeFrom)+") by \'"+input+"\' ***\n";
		for(int i=0;i<existUsersIndex.size();i++){
			write(userlist[existUsersIndex[i]].socketfd,userPipeMessage.c_str(),userPipeMessage.length());
		}	
	}
	if(hasUserPipeTo ==true && userPipeTo <=30 && userlist[userPipeTo-1].getAvailable()==true && userlist[userPipeTo-1].getUserPipeExist(user->id)==false ){
		string userPipeMessage = "*** "+user->name+" (#"+to_string(user->id)+") just piped \'"+input+"\' to "+userlist[userPipeTo-1].name+" (#"+to_string(userPipeTo)+") ***\n";
		for(int n=0;n<existUsersIndex.size();n++){
			cout << "write(userlist[" << existUsersIndex[n] << "].socketfd :" << userlist[existUsersIndex[n]].socketfd <<")\n"; //dbg
			write(userlist[existUsersIndex[n]].socketfd,userPipeMessage.c_str(),userPipeMessage.length());
		}	
	}

	// Check if there is redirection command
	for(int i=0;i<commandVec.size();i++){
		if(commandVec[i].find(">")!= string::npos){
			// There is a ">" in command
			// Now we check if there is a file name after ">"
			if((i+1)<commandVec.size() && commandVec[i].length()==1){
				needRedirection = true ;
				redirectionFileName = commandVec[i+1] ;
				cout << "Redirection File: " << redirectionFileName <<"\n"; //dbg
				break;
			}else if(commandVec[i].length() ==1){
				string temp = "syntax error near unexpected token\n";
				cerr << temp;
				write(user->socketfd,temp.c_str(),temp.length());
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

	// If this command need to userPipe to someone
	// Here we focus on  userPipe is existed or not
	// Exist: needless to create again -> print error message.
	// Not exist : Just create a corrsponding user pipe.
	}else if(hasUserPipeTo == true){
		cout << "before fork -> hasUserPipeTo==true -> check whether we can create this pipe.\n"; //dbg
		// Under this condition -> we create a user pipe
		if(userPipeTo<=30 && userlist[userPipeTo-1].getAvailable() ==true && userlist[userPipeTo-1].getUserPipeExist(user->id) ==false){
			userPipeToNewCreate = true;
			pipe(userlist[userPipeTo-1].userPipe[user->id-1]);
			cout << "A new userPipe created: " <<"userlist["<<userPipeTo-1<<"].userPipe["<<user->id-1<<"].\n"; //dbg
		}else{
			cout << "since some problem Uncreated: " <<"userlist["<<userPipeTo-1<<"].userPipe["<<user->id-1<<"].\n"; //dbg
		}

	}
	
	pid_t fork_pid = fork();

	if(fork_pid ==-1){ //fork Error
  		cout <<"fork error\n" ;
    }else if(fork_pid ==0){ // Child
   		// handle execvp argument
        for(int i=0;i<commandVec.size();i++){
			if(commandVec[i].find(">")==string::npos && commandVec[i].find("<")==string::npos && commandVec[i].find("|")==string::npos && commandVec[i].find("!")==string::npos){
				// Only part of command will be added in arg[i] 
        		arg[i] = strdup(commandVec[i].c_str());

			}else{
				//Find ">" or Find "<" or Find "|" or Find "!" :.
				// If it is ">" : We check if it is userPipe -> continue  . Else break;
				// If it is "<" : continue
				// If it is "|" or "!" : break	
				// But this function is singleProcess when we encounter the symbols above-> just break.
				break ;
			}
      	}
		// Handle STDIN_FILENO -> maybe expired number pipe || maybe userPipeFrom some user.
		if(expiredIndex != -1){
			// set pipe to STDIN and close the pipe
			close(numberPipe[expiredIndex][1]);
			dup2(numberPipe[expiredIndex][0],STDIN_FILENO);
			close(numberPipe[expiredIndex][0]);

		}else if(hasUserPipeFrom ==true){			
			// Now we check if (1) sender doesn't exist (2)userPipe doesn't exist
			cout << "Now we are in hasUserPipeFrom ==true , and userPipeFrom=" << userPipeFrom <<"\n" ;
			if(userPipeFrom >30){
				// id > 30 means sends must not exist
				// print error message of sender doesn't exist.
				string errMessage ="*** Error: user #"+to_string(userPipeFrom)+" does not exist yet. ***\n";
				write(user->socketfd,errMessage.c_str(),errMessage.length());
	
				// Still need need to execvp -> dup /dev/null to STDIN_FILENO.
				int devnull = open("/dev/null",O_RDWR);
				dup2(devnull,STDIN_FILENO);
				close(devnull);
	
			}else if(userlist[userPipeFrom-1].getAvailable() == false){
				// sender doesn't exist.	
				// print error message of sender doesn't exist.
				string errMessage ="*** Error: user #"+to_string(userPipeFrom)+" does not exist yet. ***\n";
				write(user->socketfd,errMessage.c_str(),errMessage.length());
	
				// Still need need to execvp -> dup /dev/null to STDIN_FILENO.
				int devnull = open("/dev/null",O_RDWR);
				dup2(devnull,STDIN_FILENO);
				close(devnull);

			}else if(user->getUserPipeReadExist(userPipeFrom) == false){
				// userPipe doesn't exist	
				string errMessage ="*** Error: the pipe #"+to_string(userPipeFrom)+"->#"+to_string(user->id)+" does not exist yet. ***\n";
				write(user->socketfd,errMessage.c_str(),errMessage.length());
	
				// Still need to execvp -> dup /dev/null to STDIN_FILENO.
				int devnull = open("/dev/null",O_RDWR);
				dup2(devnull,STDIN_FILENO);
				close(devnull);

			}else{
				// sender and userPipe both exist !
				// dup userPipe to STDIN_FILENO and close it and set it to -1
				cout << "interst in pipe situation : user.userPipe[userPipeFrom-1][0]=" << user->userPipe[userPipeFrom-1][0] <<"\n";
				cout << "interst in pipe situation : user.userPipe[userPipeFrom-1][1]=" << user->userPipe[userPipeFrom-1][1] <<"\n";

				close(user->userPipe[userPipeFrom-1][1]);
				dup2(user->userPipe[userPipeFrom-1][0],STDIN_FILENO);
				close(user->userPipe[userPipeFrom-1][0]);
				user->userPipe[userPipeFrom-1][1] = -1;
				user->userPipe[userPipeFrom-1][0] = -1;
				
			}	

		}		
			
		//if this child need to number pipe to another line or userPipe to someone
		if(hasNumberPipe==true){
			// dup socket to STDERR_FILENO
			dup2(user->socketfd,STDERR_FILENO);
			close(user->socketfd);

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
			//Check if userPipe create successfully
			if(userPipeToNewCreate == true){	
				cout << "We are in Child hasUserPipeTo==true -> userPipeToNewCreate==true -> broadcast\n"; //dbg
				// userPipe to receiver success -> broadcast to everyone
				cout << user->name <<"\n"; //dbg
				cout << user->id <<"\n"; //dbg
				cout << input <<"\n"; //dbg
				cout << userPipeTo << "\n"; // dbg
				cout << userlist[userPipeTo-1].name <<"\n"; //dbg

				close(userlist[userPipeTo-1].userPipe[user->id-1][0]);
				dup2(userlist[userPipeTo-1].userPipe[user->id-1][1],STDOUT_FILENO);	
				close(userlist[userPipeTo-1].userPipe[user->id-1][1]);
				userlist[userPipeTo-1].userPipe[user->id-1][0] = -1;
				userlist[userPipeTo-1].userPipe[user->id-1][1] = -1;

			}else{	
				// If here means we can't create the userPipe
				// May led by many condition -> we find the condition and send it to sender.
				cout << "we can't create userPipe -> find Why.\n"; //dbg

				if(userPipeTo >30 || (userPipeTo<=30 && userlist[userPipeTo-1].getAvailable()==false)){	
					//  means receiver must not exist
					// print error message of receiver doesn't exist.
					string errMessage ="*** Error: user #"+to_string(userPipeTo)+" does not exist yet. ***\n";
					write(user->socketfd,errMessage.c_str(),errMessage.length());
				}else if(userlist[userPipeTo-1].getUserPipeExist(user->id) == true){
					// means the user pipe already exist.
					string errMessage ="*** Error: the pipe #"+to_string(user->id)+"->#"+to_string(userlist[userPipeTo-1].id)+" already exists. ***\n";
					cout << errMessage ; // dbg
					write(user->socketfd,errMessage.c_str(),errMessage.length());
				}	

				// Still need need to execvp -> dup /dev/null to STDOUT_FILENO.
				int devnull = open("/dev/null",O_RDWR);
				dup2(devnull,STDOUT_FILENO);
				close(devnull);
			}
			
			dup2(user->socketfd,STDERR_FILENO);
			close(user->socketfd);

		}else{
			// no number pipe and no userPipe to-> output to socket
			dup2(user->socketfd,STDOUT_FILENO);
			dup2(user->socketfd,STDERR_FILENO);
			close(user->socketfd);
		}
	
		//if need to redirection -> reset the STDOUT to a file
		if(needRedirection){
			int fd = open(redirectionFileName.c_str(), O_WRONLY|O_CREAT|O_TRUNC ,S_IRUSR|S_IWUSR);
			dup2(fd,STDOUT_FILENO);
			close(fd);
		}

		//Before execvp -> Child closes the useless user pipe
		for(int i=0;i<existUsersIndex.size();i++){
			for(int j=0;j<SSOCKET_TABLE_SIZE;j++){
				close(userlist[existUsersIndex[i]].userPipe[j][0]);	
				close(userlist[existUsersIndex[i]].userPipe[j][1]);
				userlist[existUsersIndex[i]].userPipe[j][0] = -1;
				userlist[existUsersIndex[i]].userPipe[j][1] = -1;
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
			cerr << temp ; // STDERR is socket now . so it will also be passed to client.
       		exit(10);
        }	
	}else if(fork_pid >0){ //Parent
		// Parent need tidy userPipe if someone had read from it successfully.
		if(hasUserPipeFrom == true && userPipeFrom <=30 && userlist[userPipeFrom-1].getAvailable()==true && user->userPipe[userPipeFrom-1][0] != -1){
			cout << "Let look what the value is in Parent (read): " << user->userPipe[userPipeFrom-1][0] <<"\n"; //dbg
			cout << "Let look what the value is in Parent (write): " << user->userPipe[userPipeFrom-1][1] <<"\n"; //dbg

			close(user->userPipe[userPipeFrom-1][0]);
			close(user->userPipe[userPipeFrom-1][1]);

			cout << "Parent close userPipe -> User.id=" << user->id <<" userPipeFrom="<<userPipeFrom <<"\n"; //dbg 
			cout << "close(user.userPipe[" <<userPipeFrom-1 <<"][0]&[1])\n"; //dbg
			user->userPipeReset(userPipeFrom-1);

			cout << "After reset called-> the value is in Parent (read): " << user->userPipe[userPipeFrom-1][0] <<"\n"; //dbg
			cout << "After reset called the value is in Parent (write): " << user->userPipe[userPipeFrom-1][1] <<"\n"; //dbg
		}

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
void multiProcess(vector<string>commandVec,int process_count,bool hasNumberPipe,bool bothStderr,int pipeAfterLine,int numberPipe[PET_SIZE][2],int pipe_expired_table[PET_SIZE],User userlist[SSOCKET_TABLE_SIZE],User* user,bool hasUserPipeFrom,bool hasUserPipeTo,int userPipeFrom,int userPipeTo,string input){
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
	int userPipeToNewCreate = false;
	int newNumberPipeIndex ;
	int pipeToSameLine ;
	int expiredIndex = PET_findExpired(pipe_expired_table);	
	vector<int> existPipeIndex = PET_existPipe(pipe_expired_table); 
	vector<int> existUsersIndex = users_exist(userlist);

	cout << "In multiProcess now\n"; //dbg

	// handle broadcast at first time
	if(hasUserPipeFrom ==true && userPipeFrom <=30 && userlist[userPipeFrom-1].getAvailable()==true && user->getUserPipeExist(userPipeFrom)==true){
		// Broadcast
		string userPipeMessage = "*** "+user->name+" (#"+to_string(user->id)+") just received from "+userlist[userPipeFrom-1].name+" (#"+to_string(userPipeFrom)+") by \'"+input+"\' ***\n";
		for(int i=0;i<existUsersIndex.size();i++){
			write(userlist[existUsersIndex[i]].socketfd,userPipeMessage.c_str(),userPipeMessage.length());
		}	
	}
	if(hasUserPipeTo ==true && userPipeTo <=30 && userlist[userPipeTo-1].getAvailable()==true && userlist[userPipeTo-1].getUserPipeExist(user->id)==false ){
		string userPipeMessage = "*** "+user->name+" (#"+to_string(user->id)+") just piped \'"+input+"\' to "+userlist[userPipeTo-1].name+" (#"+to_string(userPipeTo)+") ***\n";
		for(int n=0;n<existUsersIndex.size();n++){
			cout << "write(userlist[" << existUsersIndex[n] << "].socketfd :" << userlist[existUsersIndex[n]].socketfd <<")\n"; //dbg
			write(userlist[existUsersIndex[n]].socketfd,userPipeMessage.c_str(),userPipeMessage.length());
		}	
	}

	// Handle argument and file redirection setting.
	cout << "commandVec.size() =" << commandVec.size() <<"\n" ; //dbg

	for(int cmd_index=0; cmd_index<commandVec.size() ; cmd_index++){
		if( commandVec[cmd_index].find("|")!= string::npos){ //Find | in this string
			if(commandVec[cmd_index].length() ==1){
				// single "|"
				process_index ++;
				process_cmd_index =0 ;
			}
		}else if(commandVec[cmd_index].find(">")!= string::npos){ //Find > in this string
			if(commandVec[cmd_index].length()==1 && (cmd_index+1) < commandVec.size()){ 
				// it is file redirection command
				needRedirection = true ;
				redirectionFileName = commandVec[cmd_index+1];
				break ;
			}else if(commandVec[cmd_index].length() >1){
				// it is >id userPipeTo command just ignore it
				continue ;

			}else{
				string temp = "syntax error near unexpected token\n";
				cerr << temp ;
				write(user->socketfd,temp.c_str(),temp.length());
				return ;
			}			
		}else if(commandVec[cmd_index].find("<")!= string::npos){ //Find < in this string
			// it is <id userPipeFrom command just ignore it
			continue ;

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
			}else if(hasUserPipeFrom == true){
				// if got hasUserPipeFrom
				// Now we check if (1) sender doesn't exist (2)userPipe doesn't exist
				cout << "Now we are in hasUserPipeFrom ==true , and userPipeFrom=" << userPipeFrom <<"\n"; //dbg
				if(userPipeFrom >30){
					// id > 30 means sends must not exist
					// print error message of sender doesn't exist.
					string errMessage ="*** Error: user #"+to_string(userPipeFrom)+" does not exist yet. ***\n";
					write(user->socketfd,errMessage.c_str(),errMessage.length());
		
					// Still need need to execvp -> dup /dev/null to STDIN_FILENO.
					int devnull = open("/dev/null",O_RDWR);
					dup2(devnull,STDIN_FILENO);
					close(devnull);
		
				}else if(userlist[userPipeFrom-1].getAvailable() == false){
					// sender doesn't exist.	
					// print error message of sender doesn't exist.
					string errMessage ="*** Error: user #"+to_string(userPipeFrom)+" does not exist yet. ***\n";
					write(user->socketfd,errMessage.c_str(),errMessage.length());
		
					// Still need need to execvp -> dup /dev/null to STDIN_FILENO.
					int devnull = open("/dev/null",O_RDWR);
					dup2(devnull,STDIN_FILENO);
					close(devnull);

				}else if(user->getUserPipeReadExist(userPipeFrom) == false){
					// userPipe doesn't exist	
					string errMessage ="*** Error: the pipe #"+to_string(userPipeFrom)+"->#"+to_string(user->id)+" does not exist yet. ***\n";
					write(user->socketfd,errMessage.c_str(),errMessage.length());
		
					// Still need to execvp -> dup /dev/null to STDIN_FILENO.
					int devnull = open("/dev/null",O_RDWR);
					dup2(devnull,STDIN_FILENO);
					close(devnull);

				}else{
					// sender and userPipe both exist !
					// dup userPipe to STDIN_FILENO and close it and set it to -1
					cout << "interst in pipe situation : user.userPipe[userPipeFrom-1][0]=" << user->userPipe[userPipeFrom-1][0] <<"\n";
					cout << "interst in pipe situation : user.userPipe[userPipeFrom-1][1]=" << user->userPipe[userPipeFrom-1][1] <<"\n";

					close(user->userPipe[userPipeFrom-1][1]);
					dup2(user->userPipe[userPipeFrom-1][0],STDIN_FILENO);
					close(user->userPipe[userPipeFrom-1][0]);
					user->userPipe[userPipeFrom-1][1] = -1;
					user->userPipe[userPipeFrom-1][0] = -1;
					
				}	
			}		

			// Child1 need to modify the stdout
			close(mPipe[0][0]);
			dup2(mPipe[0][1],STDOUT_FILENO);
			close(mPipe[0][1]);
			
			// Child 1 doesn't need slaveSocket
			// but STDERR may need it so -> dup to STDERR
			dup2(user->socketfd,STDERR_FILENO);
			close(user->socketfd);

			//Before execvp -> Child closes the useless user pipe
			for(int i=0;i<existUsersIndex.size();i++){
				for(int j=0;j<SSOCKET_TABLE_SIZE;j++){
					close(userlist[existUsersIndex[i]].userPipe[j][0]);	
					close(userlist[existUsersIndex[i]].userPipe[j][1]);
					userlist[existUsersIndex[i]].userPipe[j][0] = -1;
					userlist[existUsersIndex[i]].userPipe[j][1] = -1;
				}
			}

			// Before execvp -> close useless number Pipe
			for(int i=0;i<existPipeIndex.size();i++){
				close(numberPipe[existPipeIndex[i]][0]);
				close(numberPipe[existPipeIndex[i]][1]);
			}

			// Ready to execvp
			if(execvp(arg[0][0],arg[0]) == -1){ // execvp fail (maybe bug)
				string temp = "Unknown command: [" + string(arg[0][0]) + "].\n";
				cerr << temp ;
				exit(0);
			}

		}else if(fork_pid[0] >0) { //Parent
			// Parent need tidy userPipe if someone had read from it successfully.
			if(hasUserPipeFrom == true && userPipeFrom <=30 && userlist[userPipeFrom-1].getAvailable()==true && user->userPipe[userPipeFrom-1][0] != -1){
				cout << "Let look what the value is in Parent (read): " << user->userPipe[userPipeFrom-1][0] <<"\n"; //dbg
				cout << "Let look what the value is in Parent (write): " << user->userPipe[userPipeFrom-1][1] <<"\n"; //dbg
	
				close(user->userPipe[userPipeFrom-1][0]);
				close(user->userPipe[userPipeFrom-1][1]);
	
				cout << "Parent close userPipe -> User.id=" << user->id <<" userPipeFrom="<<userPipeFrom <<"\n"; //dbg 
				cout << "close(user.userPipe[" <<userPipeFrom-1 <<"][0]&[1])\n"; //dbg
				user->userPipeReset(userPipeFrom-1);
	
				cout << "After reset called-> the value is in Parent (read): " << user->userPipe[userPipeFrom-1][0] <<"\n"; //dbg
				cout << "After reset called the value is in Parent (write): " << user->userPipe[userPipeFrom-1][1] <<"\n"; //dbg
			}
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
			// If this command need to userPipe to someone
			// Here we focus on  userPipe is existed or not
			// Exist: needless to create again -> print error message.
			// Not exist : Just create a corrsponding user pipe.
			}else if(hasUserPipeTo == true){
				cout << "before fork -> hasUserPipeTo==true -> check whether we can create this pipe.\n"; //dbg
				// Under this condition -> we create a user pipe
				if(userPipeTo<=30 && userlist[userPipeTo-1].getAvailable() ==true && userlist[userPipeTo-1].getUserPipeExist(user->id) ==false){
					userPipeToNewCreate = true;
					pipe(userlist[userPipeTo-1].userPipe[user->id-1]);
					cout << "A new userPipe created: " <<"userlist["<<userPipeTo-1<<"].userPipe["<<user->id-1<<"].\n"; //dbg
				}else{
					cout << "since some problem Uncreated: " <<"userlist["<<userPipeTo-1<<"].userPipe["<<user->id-1<<"].\n"; //dbg
					// If here means we can't create the userPipe
					// May led by many condition -> we find the condition and send it to sender.
					if(userPipeTo >30 || (userPipeTo<=30 && userlist[userPipeTo-1].getAvailable()==false)){	
						//  means receiver must not exist
						// print error message of receiver doesn't exist.
						string errMessage ="*** Error: user #"+to_string(userPipeTo)+" does not exist yet. ***\n";
						write(user->socketfd,errMessage.c_str(),errMessage.length());
					}else if(userlist[userPipeTo-1].getUserPipeExist(user->id) == true){
						// means the user pipe already exist.
						string errMessage ="*** Error: the pipe #"+to_string(user->id)+"->#"+to_string(userlist[userPipeTo-1].id)+" already exists. ***\n";
						cout << errMessage ; // dbg
						write(user->socketfd,errMessage.c_str(),errMessage.length());
					}	
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
					dup2(user->socketfd,STDERR_FILENO);
					close(user->socketfd);

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
					//Check if userPipe create successfully
					if(userPipeToNewCreate == true){	
						cout << "We are in Child hasUserPipeTo==true -> userPipeToNewCreate==true -> broadcast\n"; //dbg
						// userPipe to receiver success -> broadcast to everyone
						cout << user->name <<"\n"; //dbg
						cout << user->id <<"\n"; //dbg
						cout << input <<"\n"; //dbg
						cout << userPipeTo << "\n"; // dbg
						cout << userlist[userPipeTo-1].name <<"\n"; //dbg

						close(userlist[userPipeTo-1].userPipe[user->id-1][0]);
						dup2(userlist[userPipeTo-1].userPipe[user->id-1][1],STDOUT_FILENO);	
						close(userlist[userPipeTo-1].userPipe[user->id-1][1]);
						userlist[userPipeTo-1].userPipe[user->id-1][0] = -1;
						userlist[userPipeTo-1].userPipe[user->id-1][1] = -1;

					}else{	

						// Still need need to execvp -> dup /dev/null to STDOUT_FILENO.
						int devnull = open("/dev/null",O_RDWR);
						dup2(devnull,STDOUT_FILENO);
						close(devnull);
					}
					
					dup2(user->socketfd,STDERR_FILENO);
					close(user->socketfd);
				}else{
					// no number pipe -> output to socket
					dup2(user->socketfd,STDOUT_FILENO);
					dup2(user->socketfd,STDERR_FILENO);
					close(user->socketfd);
				}

				if(needRedirection){
     				int fd = open(redirectionFileName.c_str(),O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR);
     				dup2(fd,STDOUT_FILENO);
     				close(fd);
				}
				
				//Before execvp -> Child closes the useless user pipe
				for(int i=0;i<existUsersIndex.size();i++){
					for(int j=0;j<SSOCKET_TABLE_SIZE;j++){
						close(userlist[existUsersIndex[i]].userPipe[j][0]);	
						close(userlist[existUsersIndex[i]].userPipe[j][1]);
						userlist[existUsersIndex[i]].userPipe[j][0] = -1;
						userlist[existUsersIndex[i]].userPipe[j][1] = -1;
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
				// if got hasUserPipeFrom
				// Now we check if (1) sender doesn't exist (2)userPipe doesn't exist
				cout << "Now we are in hasUserPipeFrom ==true , and userPipeFrom=" << userPipeFrom <<"\n"; //dbg
				if(userPipeFrom >30){
					// id > 30 means sends must not exist
					// print error message of sender doesn't exist.
					string errMessage ="*** Error: user #"+to_string(userPipeFrom)+" does not exist yet. ***\n";
					write(user->socketfd,errMessage.c_str(),errMessage.length());
		
					// Still need need to execvp -> dup /dev/null to STDIN_FILENO.
					int devnull = open("/dev/null",O_RDWR);
					dup2(devnull,STDIN_FILENO);
					close(devnull);
		
				}else if(userlist[userPipeFrom-1].getAvailable() == false){
					// sender doesn't exist.	
					// print error message of sender doesn't exist.
					string errMessage ="*** Error: user #"+to_string(userPipeFrom)+" does not exist yet. ***\n";
					write(user->socketfd,errMessage.c_str(),errMessage.length());
		
					// Still need need to execvp -> dup /dev/null to STDIN_FILENO.
					int devnull = open("/dev/null",O_RDWR);
					dup2(devnull,STDIN_FILENO);
					close(devnull);

				}else if(user->getUserPipeReadExist(userPipeFrom) == false){
					// userPipe doesn't exist	
					string errMessage ="*** Error: the pipe #"+to_string(userPipeFrom)+"->#"+to_string(user->id)+" does not exist yet. ***\n";
					write(user->socketfd,errMessage.c_str(),errMessage.length());
		
					// Still need to execvp -> dup /dev/null to STDIN_FILENO.
					int devnull = open("/dev/null",O_RDWR);
					dup2(devnull,STDIN_FILENO);
					close(devnull);

				}else{
					// sender and userPipe both exist !
					// dup userPipe to STDIN_FILENO and close it and set it to -1
					cout << "interst in pipe situation : user.userPipe[userPipeFrom-1][0]=" << user->userPipe[userPipeFrom-1][0] <<"\n";
					cout << "interst in pipe situation : user.userPipe[userPipeFrom-1][1]=" << user->userPipe[userPipeFrom-1][1] <<"\n";

					close(user->userPipe[userPipeFrom-1][1]);
					dup2(user->userPipe[userPipeFrom-1][0],STDIN_FILENO);
					close(user->userPipe[userPipeFrom-1][0]);
					user->userPipe[userPipeFrom-1][1] = -1;
					user->userPipe[userPipeFrom-1][0] = -1;
					
				}	
			}

			// Child1 need to modify the stdout
			close(mPipe[0][0]);
			dup2(mPipe[0][1],STDOUT_FILENO);
			close(mPipe[0][1]);
		
			// Child doesn't need slaveSocket but STDERR may need
			dup2(user->socketfd,STDERR_FILENO);
			close(user->socketfd);

			//Before execvp -> Child closes the useless user pipe
			for(int i=0;i<existUsersIndex.size();i++){
				for(int j=0;j<SSOCKET_TABLE_SIZE;j++){
					close(userlist[existUsersIndex[i]].userPipe[j][0]);	
					close(userlist[existUsersIndex[i]].userPipe[j][1]);
					userlist[existUsersIndex[i]].userPipe[j][0] = -1;
					userlist[existUsersIndex[i]].userPipe[j][1] = -1;
				}
			}
			// Before execvp -> close useless number Pipe
			for(int i=0;i<existPipeIndex.size();i++){
				close(numberPipe[existPipeIndex[i]][0]);
				close(numberPipe[existPipeIndex[i]][1]);
			}

			// Ready to execvp
			if(execvp(arg[process_index][0],arg[process_index]) == -1){
				cerr <<"Unknown command: ["<<arg[process_index][0]<<"].\n";
				exit(0);
			}

		}else if(fork_pid[0] >0) { //Parent
			process_index++;
			// need to regist signal for the first child
			signal(SIGCHLD,wait4children);

			// Parent need tidy userPipe if someone had read from it successfully.
			if(hasUserPipeFrom == true && userPipeFrom <=30 && userlist[userPipeFrom-1].getAvailable()==true && user->userPipe[userPipeFrom-1][0] != -1){
				cout << "Let look what the value is in Parent (read): " << user->userPipe[userPipeFrom-1][0] <<"\n"; //dbg
				cout << "Let look what the value is in Parent (write): " << user->userPipe[userPipeFrom-1][1] <<"\n"; //dbg
	
				close(user->userPipe[userPipeFrom-1][0]);
				close(user->userPipe[userPipeFrom-1][1]);
	
				cout << "Parent close userPipe -> User.id=" << user->id <<" userPipeFrom="<<userPipeFrom <<"\n"; //dbg 
				cout << "close(user.userPipe[" <<userPipeFrom-1 <<"][0]&[1])\n"; //dbg
				user->userPipeReset(userPipeFrom-1);
	
				cout << "After reset called-> the value is in Parent (read): " << user->userPipe[userPipeFrom-1][0] <<"\n"; //dbg
				cout << "After reset called the value is in Parent (write): " << user->userPipe[userPipeFrom-1][1] <<"\n"; //dbg
			}
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
					dup2(user->socketfd,STDERR_FILENO);
					close(user->socketfd);

					//Before execvp -> Child closes the useless user pipe
					for(int i=0;i<existUsersIndex.size();i++){
						for(int j=0;j<SSOCKET_TABLE_SIZE;j++){
							close(userlist[existUsersIndex[i]].userPipe[j][0]);	
							close(userlist[existUsersIndex[i]].userPipe[j][1]);
							userlist[existUsersIndex[i]].userPipe[j][0] = -1;
							userlist[existUsersIndex[i]].userPipe[j][1] = -1;
						}
					}
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
			}else if(hasUserPipeTo ==true){
				cout << "before fork -> hasUserPipeTo==true -> check whether we can create this pipe.\n"; //dbg
				// Under this condition -> we create a user pipe
				if(userPipeTo<=30 && userlist[userPipeTo-1].getAvailable() ==true && userlist[userPipeTo-1].getUserPipeExist(user->id) ==false){
					userPipeToNewCreate = true;
					pipe(userlist[userPipeTo-1].userPipe[user->id-1]);
					cout << "A new userPipe created: " <<"userlist["<<userPipeTo-1<<"].userPipe["<<user->id-1<<"].\n"; //dbg
				}else{
					cout << "since some problem Uncreated: " <<"userlist["<<userPipeTo-1<<"].userPipe["<<user->id-1<<"].\n"; //dbg
					// If here means we can't create the userPipe
					// May led by many condition -> we find the condition and send it to sender.
					if(userPipeTo >30 || (userPipeTo<=30 && userlist[userPipeTo-1].getAvailable()==false)){	
						//  means receiver must not exist
						// print error message of receiver doesn't exist.
						string errMessage ="*** Error: user #"+to_string(userPipeTo)+" does not exist yet. ***\n";
						write(user->socketfd,errMessage.c_str(),errMessage.length());
					}else if(userlist[userPipeTo-1].getUserPipeExist(user->id) == true){
						// means the user pipe already exist.
						string errMessage ="*** Error: the pipe #"+to_string(user->id)+"->#"+to_string(userlist[userPipeTo-1].id)+" already exists. ***\n";
						cout << errMessage ; // dbg
						write(user->socketfd,errMessage.c_str(),errMessage.length());
					}	
				}	
			}

			if((last_process=fork())==-1){ //
				cout <<"last process fork error:"<<process_index<<"\n";
			}else if(last_process ==0){ //child
				//if this child need to number pipe to another line
				if(hasNumberPipe==true){
					// dup socket to STDERR_FILENO
					dup2(user->socketfd,STDERR_FILENO);
					close(user->socketfd);

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
					//Check if userPipe create successfully
					if(userPipeToNewCreate == true){	
						cout << "We are in Child hasUserPipeTo==true -> userPipeToNewCreate==true -> broadcast\n"; //dbg
						// userPipe to receiver success -> broadcast to everyone
						cout << user->name <<"\n"; //dbg
						cout << user->id <<"\n"; //dbg
						cout << input <<"\n"; //dbg
						cout << userPipeTo << "\n"; // dbg
						cout << userlist[userPipeTo-1].name <<"\n"; //dbg

						close(userlist[userPipeTo-1].userPipe[user->id-1][0]);
						dup2(userlist[userPipeTo-1].userPipe[user->id-1][1],STDOUT_FILENO);	
						close(userlist[userPipeTo-1].userPipe[user->id-1][1]);
						userlist[userPipeTo-1].userPipe[user->id-1][0] = -1;
						userlist[userPipeTo-1].userPipe[user->id-1][1] = -1;

					}else{	
						// Still need need to execvp -> dup /dev/null to STDOUT_FILENO.
						int devnull = open("/dev/null",O_RDWR);
						dup2(devnull,STDOUT_FILENO);
						close(devnull);
					}
					
					dup2(user->socketfd,STDERR_FILENO);
					close(user->socketfd);
				
				}else{
					// no number pipe -> output to socket
					dup2(user->socketfd,STDOUT_FILENO);
					dup2(user->socketfd,STDERR_FILENO);
					close(user->socketfd);
				}
			
				close(mPipe[(process_index-1)%2][1]); //close front write
				dup2(mPipe[(process_index-1)%2][0],STDIN_FILENO); //dup front read to STDIN
				close(mPipe[(process_index-1)%2][0]); // close front read
				
				if(needRedirection){
     				int fd = open(redirectionFileName.c_str(),O_WRONLY|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR);
     				dup2(fd,STDOUT_FILENO);
     				close(fd);
				}

				//Before execvp -> Child closes the useless user pipe
				for(int i=0;i<existUsersIndex.size();i++){
					for(int j=0;j<SSOCKET_TABLE_SIZE;j++){
						close(userlist[existUsersIndex[i]].userPipe[j][0]);	
						close(userlist[existUsersIndex[i]].userPipe[j][1]);
						userlist[existUsersIndex[i]].userPipe[j][0] = -1;
						userlist[existUsersIndex[i]].userPipe[j][1] = -1;
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
void callPrintenv(string envVar,User user){
	const char* input = envVar.c_str() ;
	char* path_string = getenv (input);

	if (path_string!=NULL){
		cout <<path_string<<endl;
		string temp(path_string);
		temp += "\n" ;
		
		// Send environment variable to user's socket.
		write(user.socketfd,temp.c_str(),temp.length());
	}
}

void callSetenv(string envVar,string value){
	const char *envname = envVar.c_str();
	const char *envval = value.c_str();

	setenv(envname,envval,1);
}

// signal handler
void wait4children(int signo){
	int status;
	while(waitpid(-1,&status,WNOHANG)>0);
}

int users_findEmpty(User users[SSOCKET_TABLE_SIZE]){
	int result =-1 ;
	
	for(int i=0;i<SSOCKET_TABLE_SIZE;i++){
		if(users[i].getAvailable() == false){
			result =i;
			break;
		}
	}
	return result; 
}
vector<int> users_exist(User users[SSOCKET_TABLE_SIZE]){
	vector<int> result ;
	for(int i=0;i<SSOCKET_TABLE_SIZE;i++){
		if(users[i].getAvailable() == true){
			result.push_back(i);
		}
	}
	return result;
}
string extractClientInput(char buffer[MAX_LENGTH],int readCount){
	string result ="";

	for(int i=0;i<readCount;i++){
		if(buffer[i]== '\n' || buffer[i] =='\r' || buffer[i] =='\0'){
			break;			
		}
		result += buffer[i];
	}
	return result;
}

void clearMessageUserPass(User logoutUser,User userlist[SSOCKET_TABLE_SIZE]){
	// When user logout -> need to clear all message this user pass to
	vector<int> onlineUserIndex = users_exist(userlist);
	for(int i=0;i<onlineUserIndex.size();i++){
		if(userlist[onlineUserIndex[i]].getUserPipeExist(logoutUser.id) == true){
			close(userlist[onlineUserIndex[i]].userPipe[logoutUser.id-1][0]);
			close(userlist[onlineUserIndex[i]].userPipe[logoutUser.id-1][1]);
			userlist[onlineUserIndex[i]].userPipe[logoutUser.id-1][0] = -1;
			userlist[onlineUserIndex[i]].userPipe[logoutUser.id-1][1] = -1;
		}		
	}

}

void dbug(int line){
	cout << "line: " << line <<"\n";
}
