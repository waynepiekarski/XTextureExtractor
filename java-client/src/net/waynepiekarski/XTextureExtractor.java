// ---------------------------------------------------------------------
//
// XTextureExtractor
//
// Copyright (C) 2018 Wayne Piekarski
// wayne@tinmith.net http://tinmith.net/wayne
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// ---------------------------------------------------------------------

package net.waynepiekarski;

import javax.imageio.ImageIO;
import javax.swing.*;
import java.awt.*;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.awt.image.BufferedImage;
import java.io.*;
import java.net.Socket;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.ListIterator;

public class XTextureExtractor extends JFrame {

    static final int TCP_PORT = 52500;
    static final int TCP_INTRO_HEADER = 4096;
    static final String TCP_PLUGIN_VERSION = "XTEv3";

    public JLabel mLabel;
    public JFrame mFrame;
    static public int windowActive = -1;
    static public boolean windowFullscreen = false;
    String windowAircraft;
    Boolean windowPacked = false;
    ArrayList<String> windowNames = new ArrayList<String>();

    public XTextureExtractor(String hostname) {
        mFrame = this;
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setTitle("XTextureExtractor");

        if (windowFullscreen) {
            setExtendedState(JFrame.MAXIMIZED_BOTH);
            setUndecorated(true);
            setVisible(true);
            System.err.println("Fullscreen frame is " + mFrame.getWidth() + "x" + mFrame.getHeight());
        }

        mLabel = new JLabel();
        add(mLabel, BorderLayout.CENTER);

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
            // Each window transmission starts with !_____X_ where X is a binary byte 0x00 to 0xFF
            int windowId = -1;
            int expectedBytes = -1;
            try {
                // We sometimes end up with PNG data, run a sliding window until we find the next header
                char a = (char)dataInputStream.readByte();
                char b = (char)dataInputStream.readByte();
                char c = (char)dataInputStream.readByte();
                char d = (char)dataInputStream.readByte();
                char e = (char)dataInputStream.readByte();
                char f = (char)dataInputStream.readByte();
                char g = (char)dataInputStream.readByte();
                char h = (char)dataInputStream.readByte();
                if ((a != '!') || (b != '_') || (c != '_') || (d != '_') || (e != '_') || (f != '_') || (h != '_')) {
                    System.err.println("Image header invalid ![" + a + "] _[" + b + "] _[" + c + "] _[" + d + "] _[" + e + "] _[" + f + "] W[" + windowId + "] _[" + h + "]");
                    System.exit(1);
                }
                windowId = (int) g;

                // Read the number of upcoming PNG bytes
                int b0 = (int)(dataInputStream.readByte()) & 0xFF;
                int b1 = (int)(dataInputStream.readByte()) & 0xFF;
                int b2 = (int)(dataInputStream.readByte()) & 0xFF;
                int b3 = (int)(dataInputStream.readByte()) & 0xFF;
                expectedBytes = (((((b3 * 256) + b2) * 256) + b1) * 256) + b0;

                char w = (char)dataInputStream.readByte();
                char x = (char)dataInputStream.readByte();
                char y = (char)dataInputStream.readByte();
                char z = (char)dataInputStream.readByte();
                if ((w != '_') || (x != '_') || (y != '_') || (z != '_')) {
                    System.err.println("Image second header invalid _[" + w + "] _[" + x + "] _[" + y + "] _[" + z + "]");
                    System.exit(1);
                }
            } catch (IOException e) {
                System.err.println("Failed to receive window header, connection has failed");
                break;
            }

            try {
                if (windowId != windowActive) {
                    // Skip the PNG data if the window is not active
                    dataInputStream.skipBytes(expectedBytes);
                } else {
                    // Read the expected PNG data into a buffer
                    byte[] pngData = new byte[expectedBytes];
                    dataInputStream.readFully(pngData);
                    ByteArrayInputStream byteArrayInputStream = new ByteArrayInputStream(pngData);

                    // Read the raw PNG data and put it in the window if it is a match
                    BufferedImage _image = ImageIO.read(byteArrayInputStream);
                    int iw = _image.getWidth();
                    int ih = _image.getHeight();
                    Image image = _image;

                    // System.err.println("Storing image " + windowId);

                    // If the window has been laid out once, then resize the image to fit this
                    if (windowPacked || windowFullscreen) {
                        int lw = label.getWidth();
                        int lh = label.getHeight();
                        if ((lw <= 1) || (lh <= 1)) {
                            lw = iw;
                            lh = ih;
                            System.err.println("Fixing up empty image to size " + lw + "x" + lh);
                        }
                        image = image.getScaledInstance(lw, lh, Image.SCALE_SMOOTH);
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
                }

                // Read the ending padding nulls to 1024 bytes
                try {
                    int skip = 1024 - (expectedBytes % 1024);
                    // Log.d(Const.TAG, "Skipping " + skip + " bytes to pad to 1024 bytes");
                    dataInputStream.skipBytes(skip);
                } catch (IOException e) {
                    System.err.println("Failure during padding receive, connection has failed");
                    System.exit(1);
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
