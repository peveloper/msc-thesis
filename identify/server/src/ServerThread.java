import java.util.*;
import java.io.*;
import java.net.*;
import org.apache.commons.math3.stat.correlation.PearsonsCorrelation;

public class ServerThread implements Runnable {

	private Socket clientSocket;
	private KDTree windowDB;
    private short windowSize;
    private short keySize;
    private short keyMode;

	public ServerThread(Socket clientSocket, KDTree windowDB, short windowSize, short keySize, short keyMode) {
		this.clientSocket = clientSocket;
		this.windowDB = windowDB;
        this.windowSize = windowSize;
        this.keySize = keySize;
        this.keyMode = keyMode;
	}

	@Override
	public void run() {
		PearsonsCorrelation correlator = new PearsonsCorrelation();

		String ipAddr = clientSocket.getInetAddress().toString();

		System.out.println(ipAddr + "\tconnected");

		PrintWriter out = null;
		Scanner in = null;

		try {
			out = new PrintWriter(clientSocket.getOutputStream(), true); 
			in = new Scanner(clientSocket.getInputStream());
		} catch (IOException e) {
			System.err.println(ipAddr + "\treader/writer failed"); 
			return; 
		} 

		while (in.hasNextLine()) {

			String inputLine = in.nextLine();

			if (inputLine.equals("complete")) {
				System.out.println(ipAddr + "\tcomplete");
				break;
			}

            Movie sampleMovie = new Movie("sampleTitle\t0\t" + inputLine, windowSize);
			Window currentWindow = new Window(sampleMovie, (short) 0, keySize);

			float[] key = currentWindow.generateKey(keyMode);

            System.out.println(currentWindow.toString());

            float [] lowerKey = new float [keySize];
            float [] upperKey = new float [keySize];

            switch (keyMode) {
                case 0:
                    lowerKey[0] = key[0] - (windowSize * 525);
                    upperKey[0] = key[0] + (windowSize * 525);
                default:
                    lowerKey[0] = key[0] - (50000);
                    upperKey[0] = key[0] + (50000);
            }

            for (int i = 1; i < keySize; i++) {
                lowerKey[i] = key[i] - 0.01f;
                upperKey[i] = key[i] + 0.01f;
            }

			Object[] shortList = windowDB.getRange(lowerKey, upperKey);

			int[] currentSegments = currentWindow.getSegments();

			String result = "none";
			for (int i = 0; i < shortList.length; i++) {
				Window compareWindow = (Window)shortList[i];
				int compareStart = compareWindow.getStartIndex();
				int[] compareSegments = compareWindow.getSegments();

				double[] currentDoubles = new double[windowSize];
				double[] compareDoubles = new double[windowSize];

				for (int y = 0; y < currentSegments.length; y++) {
					currentDoubles[y] = (double)currentSegments[y];
					compareDoubles[y] = (double)compareSegments[compareStart + y];
				}

				double segmentCorrel = correlator.correlation(currentDoubles, compareDoubles);

				if (segmentCorrel > 0.9999) {
                    System.out.println(segmentCorrel);
                    System.out.println(compareWindow.getTitle());
                    System.out.println(compareWindow.getBitrate());

                    float compareWindowKey[] = compareWindow.generateKey(keyMode);
                    ArrayList<Float> diff = new ArrayList<Float>();

                    for(int j =0; j < key.length; j++) {
                        diff.add(Math.abs(key[j] - compareWindowKey[j]));
                    }

                    int key_idx = diff.indexOf(Collections.min(diff)); 
                    
					result = compareWindow.getTitle() + "\t" + 
                             compareWindow.getBitrate() + "\t" +
                             compareWindow.getStartIndex() + "\t" +
                             //key_idx + "\t" +
                             //compareWindow.toString() + "\t" +
                             //currentWindow.toString() + "\t" +
                             segmentCorrel;
					break;
				}
			}
			out.println(result);
		}
		try {
			out.close();
			in.close();
			clientSocket.close();
		} catch (IOException e) { 
			System.err.println(ipAddr + "\tclean-up failed"); 
		} 
		System.out.println(ipAddr + "\tdisconnected");
	}
}
