#include "streamer.h"

#include <signal.h>
#include <sys/poll.h>


bool Streamer::keepServing;
char Streamer::currentCommand;
bool Streamer::commandRecieved;
bool Streamer::frameSourceBool;
bool Streamer::selectionRecieved;
cv::Mat Streamer::currentFrame;
cv::Rect Streamer::currentSelection;
cv::VideoCapture Streamer::frameSource;
std::vector<std::thread> Streamer::workerList;



/* Function to clean up threads */
void Streamer::EndConnection() {
	/* Notifying service has ended and shutting down the socket */
	Streamer::keepServing = false;
	shutdown(serverSocket, SHUT_RDWR);
	close(serverSocket);

	/* Making sure each thread has ended its job */
	for (int i=0; i<workerList.size(); ++i) {
		workerList[i].join();
	}
	redirectorThread.join();
}



/* Returns the status flag that indicates the arrival of new selection */
bool Streamer::SelectionChanged() {
	return selectionRecieved;
}



/* Returns the last recieved selection and resets the flag */
cv::Rect Streamer::GetNewSelection() {
	selectionRecieved = false;
	return currentSelection;
}



/*  */
bool Streamer::CommandRecieved() {
	return commandRecieved;
}



/*  */
char Streamer::GetCommand() {
	commandRecieved = false;
	return currentCommand;
}
	
	

/*  */
void Streamer::SendFrame(cv::Mat newFrame) {
	newFrame.copyTo(currentFrame);
}



/* Creates a socket with given ip and port settings to communicate with clients */
void Streamer::CreateConnection(string port) {
	struct sockaddr_in socketAddress;
	int set = 1;


	/* Creating a socket of desired type */
	serverSocket = socket(AF_INET, SOCK_STREAM, DEFAULT_OPTIONS);
	if (serverSocket == ERROR_CODE) {
		std::cerr << "\nError\nServer socket creation failed\n";
		exit(EXIT_FAILURE);
	}

	/* Changing socket options to set port as reuasble */
	if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &set, sizeof(int)) == ERROR_CODE) {
		std::cerr << "\nError\nReusable port options couldn't set\n";
		exit(EXIT_FAILURE);
	}

	/* Filling socket address structure */
	socketAddress.sin_family = AF_INET;
	socketAddress.sin_addr.s_addr = inet_addr("0.0.0.0");
	socketAddress.sin_port = htons(stoi(port));

	/* Binding socket descriptor to socket address */
	if (bind(serverSocket, (struct sockaddr *)&socketAddress, sizeof(struct sockaddr)) == ERROR_CODE) {
		std::cerr << "\nError\nServer address binding failed\n";
		exit(EXIT_FAILURE);
	}
	
	frameSourceBool = false;
	//std::cout << "Server is ready to stream on " << ip << ":" << port << std::endl;
}



/* Sets the client limit of the socket created, should be called after CreateConnection() */
void Streamer::ListenConnectionPoint(int clientLimit) {
	/* Listening bound socket for maximum of given amount of clients */
	if (listen(serverSocket, clientLimit) == ERROR_CODE) {
		std::cerr << "\nError\nServer failed to listen bound socket\n";
		exit(EXIT_FAILURE);
	}

	Streamer::keepServing = true;
	redirectorThread = std::thread(AcceptClients, serverSocket);
	std::cout << "Client limit set to " << clientLimit << std::endl;
}



/* Setter for cv::VideoCapture member to get the frames to send to clients */
void Streamer::SetCaptureSource(cv::VideoCapture newSource) {
	
	frameSource = newSource;
	frameSourceBool = true;
}



/* Thread function to wait for incoming selections from clients */
void Streamer::RecieveMessage(int socket) {
	struct ClientMessage msg;
	cv::Rect selection;
	int bytes;


	do {
		/* If a valid recieving has done, set the recieved selection and raise flag */
		bytes = recv(socket, &msg, sizeof(msg), DEFAULT_OPTIONS);
		if (bytes > ZERO) {
			currentCommand = msg.message;
			commandRecieved = true;
			if (currentCommand == MESSAGE_NEW_SELECTION) {
					currentSelection = msg.selection;
					selectionRecieved = true;
					std::cout << "New selection recieved" << std::endl;		
			}
			
			else {
				std::cout << "New command recieved" << std::endl;
			}
		}
	} while (bytes > ZERO && keepServing);
}



/* Thread function to infinitly accept clients from the created socket */
void Streamer::AcceptClients(int socket) {
	int clientSocket;


	while(keepServing) {
		/* Get new client socket and check for errors */
		clientSocket = accept(socket, nullptr, nullptr);
		if (clientSocket == ERROR_CODE) {
			if (errno != EINVAL)
				std::cerr << "\nError\nConnection attempt with client failed\n" << errno << std::endl;
		}

		/* Create a streamer thread and a reciever thread to both stream and wait for new selection */
		else {
			std::cout << "Creating new thread for client #" << clientSocket << std::endl;
			std::thread sender(ServeClient, clientSocket);
			std::thread reciever(RecieveMessage, clientSocket);
			workerList.push_back(std::move(sender));
			workerList.push_back(std::move(reciever));
		}
	}
}



/* Thread function to send the client frames from the video source */
void Streamer::ServeClient(int client) {
	cv::Mat frame;
	uint32_t size;
	std::vector<uchar> buff;
	std::vector<int> param;
	param.push_back(cv::IMWRITE_JPEG_QUALITY);
	param.push_back(80);


	while(keepServing) {
		if (frameSourceBool)
			frameSource >> frame;
		else 
			currentFrame.copyTo(frame);
			
		/* Compressing current frame */
		cv::imencode(".jpg", frame, buff, param);
		size = sizeof(uchar) * buff.size();
		uchar package[size];
		for (int i=0; i<buff.size(); ++i)
			package[i] = buff[i];

		/* Send data size and data */
		if (send(client, &size, sizeof(size), DEFAULT_OPTIONS) <= ZERO) {
			break;
		}

		if (send(client, package, size, DEFAULT_OPTIONS) <= ZERO) {
			break;
		}
	}


	shutdown(client, SHUT_RDWR);
	std::cerr << "Connection closed with client #" << client << std::endl;
}
