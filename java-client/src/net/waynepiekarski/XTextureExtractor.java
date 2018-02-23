package net.waynepiekarski;

import javax.imageio.ImageIO;
import javax.swing.*;
import java.awt.*;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.io.*;
import java.net.Socket;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.ListIterator;

public class XTextureExtractor extends JFrame {

    static final int TCP_PORT = 52500;
    static final int TCP_INTRO_HEADER = 4096;
    static final String TCP_PLUGIN_VERSION = "XTEv2";

    public JLabel mLabel;
    public JFrame mFrame;
    static public int windowActive = -1;
    static public boolean windowFullscreen = false;
    String windowAircraft;
    Boolean windowPacked = false;
    ArrayList<String> windowNames = new ArrayList<String>();

    public XTextureExtractor(String hostname) {
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setTitle("XTextureExtractor");

        if (windowFullscreen) {
            setExtendedState(JFrame.MAXIMIZED_BOTH);
            setUndecorated(true);
            setVisible(true);
        }

        mLabel = new JLabel();
        add(mLabel, BorderLayout.CENTER);
        pack();
        mFrame = this;

        mLabel.addMouseListener(new MouseAdapter()
        {
            public void mouseClicked(MouseEvent e)
            {
                windowActive++;
                if (windowActive >= windowNames.size())
                    windowActive = 0;
                windowPacked = false;
                System.err.println("Detected window click, adjusted to " + windowActive);
            }
        });

        Thread thread = new Thread(new Runnable() {
            @Override
            public void run() {
                networkLoop(hostname, mLabel);
            }
        });
        thread.start();
    }

    public void networkLoop(String hostname, JLabel label) {
        Boolean cancelled = false;

        System.err.println("Connecting to " + hostname + " port " + TCP_PORT);
        DataInputStream dataInputStream;
        try {
            Socket socket = new Socket(hostname, TCP_PORT);
            dataInputStream = new DataInputStream(socket.getInputStream());
        } catch (IOException e) {
            System.err.println("Failed to make connection");
            cancelled = true;
            dataInputStream = null;
        }
        System.err.println("Connection established");

        // There is always a header at the start before the PNG stream, read that first
        if (!cancelled) {
            byte[] header = new byte[TCP_INTRO_HEADER];
            try {
                System.err.println("Waiting for header");
                dataInputStream.readFully(header);
                String headerStr = new String(header);
                headerStr = headerStr.substring(0, headerStr.indexOf(0x00));
                System.err.println("Received raw header [" + headerStr + "] before PNG stream");
                try {
                    BufferedReader bufferedReader = new BufferedReader(new InputStreamReader(new ByteArrayInputStream(header)));
                    String version = bufferedReader.readLine().split(" ")[0];
                    windowAircraft = bufferedReader.readLine();
                    String[] texture = bufferedReader.readLine().split(" ");
                    int textureWidth = Integer.parseInt(texture[0]);
                    int textureHeight = Integer.parseInt(texture[1]);
                    System.err.println("Plugin version [" + version + "], aircraft [" + windowAircraft + "], texture " + textureWidth + "x" + textureHeight);
                    String line = null;
                    windowNames.clear();
                    while (true) {
                        line = bufferedReader.readLine();
                        if (line == null || line.contains("__EOF__"))
                            break;
                        String[] window = line.split(" ");
                        String name = window[0];
                        windowNames.add(name);
                        int l = Integer.parseInt(window[1]);
                        int t = Integer.parseInt(window[2]);
                        int r = Integer.parseInt(window[3]);
                        int b = Integer.parseInt(window[4]);
                        System.err.println("Window [" + name + "] = (" + l + "," + t + ")->(" + r + "," + b + ")");
                    }
                    if (!version.equals(TCP_PLUGIN_VERSION)) {
                        System.err.println("Version [" + version + "] is not expected [" + TCP_PLUGIN_VERSION + "]");
                        System.exit(1);
                    }
                    if (windowNames.size() <= 0) {
                        System.err.println("No valid windows were sent");
                        System.exit(1);
                    }
                    if (windowActive < 0) {
                        windowActive = 0;
                    } else if (windowActive >= windowNames.size()) {
                        System.err.println("Manual window id " + windowActive + " is out of bounds " + windowNames.size() + " so setting to 0");
                        windowActive = 0;
                    }
                } catch (IOException e) {
                    System.err.println("IOException invalid header data - " + e);
                    System.exit(1);
                } catch(Exception e){
                    System.err.println("Unknown exception invalid header read - " + e);
                    System.exit(1);
                }
            } catch (IOException e) {
                System.err.println("Failed to receive initial header, connection has failed");
                cancelled = true;
            }
        }

        while (!cancelled) {
            // Each window transmission starts with !_X_ where X is a binary byte 0x00 to 0xFF
            int windowId = -1;
            try {
                // We end up with some remaining PNG data that we need to eat up
                char a = 0x00;
                while (a != '!') {
                    a = (char) dataInputStream.readByte();
                    // System.err.println("Eating up extra PNG ending " + (((int)a) & 0xFF));
                }
                char b = (char) dataInputStream.readByte();
                windowId = (int) dataInputStream.readByte();
                char d = (char) dataInputStream.readByte();
                if ((a != '!') || (b != '_') || (d != '_')) {
                    System.err.println("Image header invalid ![" + (((int) a) & 0xFF) + "d/" + a + "] _[" + (byte) b + "d/" + b + "] _[" + (byte) d + "d/" + d + "]");
                    break;
                }
            } catch (IOException e) {
                System.err.println("Failed to receive window header, connection has failed");
                break;
            }

            try {
                // Read the raw PNG data and put it in the window if it is a match
                Image image = ImageIO.read(dataInputStream);
                if (windowId == windowActive) {
                    // System.err.println("Storing image " + windowId);

                    // If the window has been laid out once, then resize the image to fit this
                    if (windowPacked || windowFullscreen) {
                        image = image.getScaledInstance(label.getWidth(), label.getHeight(), Image.SCALE_SMOOTH);
                    }

                    // Store the image into an icon for display
                    ImageIcon ic = new ImageIcon(image);
                    label.setIcon(ic);

                    // If the window has a new image then pack it to do layout
                    if (!windowPacked && !windowFullscreen) {
                        mFrame.setTitle("XTextureExtractor: " + windowAircraft + " " + windowNames.get(windowActive));
                        mFrame.pack();
                        windowPacked = true;
                    }
                } else {
                    // System.err.println("Ignoring image " + windowId);
                }
            } catch (IOException e) {
                System.err.println("Failed to read and decode image");
                cancelled = true;
                break;
            }
        }

        System.err.println("Network loop failed");
        System.exit(1);
    }

    public static void main(String[] args) {
        ArrayList<String> list = new ArrayList<>(Arrays.asList(args));
        ListIterator<String> iter = list.listIterator();
        while(iter.hasNext()) {
            String s = iter.next();
            if (s.startsWith("--window")) {
                s = s.substring("--window".length());
                windowActive = Integer.parseInt(s);
                System.err.println("Specify window id " + windowActive);
                iter.remove();
            } else if (s.equals("--fullscreen")) {
                System.err.println("Selected full screen mode");
                windowFullscreen = true;
                iter.remove();
            } else {
                System.err.println("Leaving argument " + s);
            }
        }
        if (list.isEmpty()) {
            System.err.println("XTextureExtractor, streams PNGs from port " + TCP_PORT);
            System.err.println("Requires an argument: <hostname> [--fullscreen] [--windowN]");
            System.exit(1);
        }
        final String _hostname = list.get(0);

        SwingUtilities.invokeLater(new Runnable() {
            @Override
            public void run() {
                new XTextureExtractor(_hostname).setVisible(true);
            }
        });
    }
}
