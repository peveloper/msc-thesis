import java.util.*;
import java.io.*;
import java.net.*;

public class Netflid {

	public static void main(String[] args) {


        if (Short.parseShort(args[1]) != 0) {
        
            final short WINDOW_SIZE = Short.parseShort(args[1]);

            final short KEY_SIZE = Short.parseShort(args[2]);

            if (KEY_SIZE > WINDOW_SIZE + 1 || KEY_SIZE == 0) {
                System.err.println("KEY ERROR");
                System.exit(1);
            }

            final short KEY_MODE = Short.parseShort(args[3]);

            KDTree windowDB = new KDTree(KEY_SIZE, 48);

            FileInputStream movieListIS = null;

            try {
                movieListIS = new FileInputStream(args[0]);
            } catch (Exception e) {
                System.out.println("ERROR: Unable to open the fingerprint file.");
                System.exit(0);
            }

            Scanner dataInput = new Scanner(movieListIS);

            while (dataInput.hasNextLine()) {

                Movie currentMovie = new Movie(dataInput.nextLine(), WINDOW_SIZE);

                int numWindows = currentMovie.getNumWindows();

                for (short i = 0; i < numWindows; i++) {
                    Window currentWindow = new Window(currentMovie, i, KEY_SIZE);
                    windowDB.add(currentWindow.generateKey(KEY_MODE), currentWindow);
                }

            }

            dataInput.close();

            ServerSocket serverSocket = null; 

            try { 
                serverSocket = new ServerSocket(10007); 
            } catch (IOException e) { 
                System.err.println("Could not listen on port: 10007."); 
                System.exit(1); 
            } 

            System.out.println("Server started");

            while (true) {
                Socket clientSocket = null; 

                try { 
                    clientSocket = serverSocket.accept(); 

                    ServerThread serverThread = new ServerThread(clientSocket, windowDB, WINDOW_SIZE, KEY_SIZE, KEY_MODE);

                    Thread serverThread1 = new Thread(serverThread, "server");

                    serverThread1.start();
                } catch (IOException e) { 
                    System.err.println("Accept failed."); 
                }
            }
        } else {
            System.err.println("INPUT ERROR: WINDOW_SIZE must be greater than 5");
        }
    }
}
