public class Movie {

	private String title;
	private short bitrate;
	private int[] segments;
	private short numWindows;
    private short windowSize;

	// Constructor
	public Movie(String titleBitrateAndFingerprint, short windowSize) {

        this.windowSize = windowSize;

		String[] titleBitrateAndFingerprintArray = titleBitrateAndFingerprint.split("\\t");

		title = titleBitrateAndFingerprintArray[0];

		bitrate = Short.parseShort(titleBitrateAndFingerprintArray[1]);

		String[] fingerprintAsStringArray = titleBitrateAndFingerprintArray[2].split(",");
		segments = new int[fingerprintAsStringArray.length];
		for (int i = 0; i < fingerprintAsStringArray.length; i++) {
			segments[i] = Integer.parseInt(fingerprintAsStringArray[i]);
		}

		numWindows = (short)(segments.length - windowSize + 1);
	}

	public String getTitle() {
		return title;
	}

	public short getBitrate() {
		return bitrate;
	}

	public int[] getSegments() {
		return segments;
	}

	public short getNumWindows() {
		return numWindows;
	}

    public short getWindowSize() {
        return windowSize;
    }
}
