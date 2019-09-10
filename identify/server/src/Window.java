import java.util.*;
import java.util.Arrays;
import java.util.List;

public class Window {

    private Movie parentMovie;
	private short startIndex;
    private short keySize;
    private float [] key;
    private int [] segments;
    private short windowSize;


	public Window(Movie parentMovie, short startIndex, short keySize) {
		this.parentMovie = parentMovie;
		this.startIndex = startIndex;
        this.keySize = keySize;
	}

	public String getTitle() {
		return parentMovie.getTitle();
	}

	public short getBitrate() {
		return parentMovie.getBitrate();
	}

	public int[] getSegments() {
		return parentMovie.getSegments();
	}

	public int getStartIndex() {
		return (int)startIndex;
	}

    public float[] generateKey(short keyMode) {
        switch (keyMode) {
            case 0:
                return this.getTotalSizeKey();
            default:
                return this.getAVGBitrateKey();
        }
    }

    public float[] getAVGBitrateKey() {
		segments = parentMovie.getSegments();

        windowSize = parentMovie.getWindowSize();

		key = new float[keySize];

        int step = windowSize / keySize;

        for (int i = 0; i < windowSize; i++) {
            int segment = segments[startIndex + i];
            key[(i + 1) % keySize] += segment;
            key[0] += segment;
        }

        //AVB Bitrate in WINDOW
        key[0] = 8 * (key[0] / windowSize / 4);

        //Normalize by AVG Bitrate 
        for (int i = 1; i < keySize; i++ ){
            key[i] = key[i] / key[0];
        }

		return key;

    }

    public float[] getTotalSizeKey() {
        segments = parentMovie.getSegments();

        windowSize = parentMovie.getWindowSize();

        key = new float[keySize];

        for (int i = 0; i < windowSize; i++) {
            int segment = segments[startIndex + i];
            key[(i + 1) % keySize] += segment;
            key[0] += segment;
        }

        // Normalize by total size of segments in the window
        for (int i = 1; i < keySize; i++ )
            key[i] = key[i] / key[0];

        return key;
    }

    @Override
    public String toString() {
        ArrayList<String> keyList = new ArrayList<String>();

        for (int i = 0; i < keySize; i++) {
            keyList.add(String.valueOf(key[i]));
        }

        return String.join(" ", keyList);
    }
}
