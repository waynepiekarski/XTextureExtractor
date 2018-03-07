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

package net.waynepiekarski.xtextureextractor

import android.graphics.Bitmap
import android.util.Log

import java.net.*
import kotlin.concurrent.thread
import java.io.*
import android.graphics.BitmapFactory
import android.os.Build
import android.widget.Toast


class TCPBitmapClient (private var address: InetAddress, private var port: Int, private var callback: OnTCPBitmapEvent) {
    private lateinit var socket: Socket
    @Volatile private var cancelled = false
    private lateinit var bufferedWriter: BufferedWriter
    private lateinit var outputStreamWriter: OutputStreamWriter
    private lateinit var dataInputStream: DataInputStream

    interface OnTCPBitmapEvent {
        fun onReceiveTCPBitmap(windowId: Int, image: Bitmap, tcpRef: TCPBitmapClient)
        fun onReceiveTCPHeader(header: ByteArray, tcpRef: TCPBitmapClient)
        fun onConnectTCP(tcpRef: TCPBitmapClient)
        fun onDisconnectTCP(reason: String?, tcpRef: TCPBitmapClient)
        fun getWindow1Index(): Int
        fun getWindow2Index(): Int
    }

    fun stopListener() {
        // Stop the loop from running any more
        cancelled = true

        // Call close on the top level buffers to cause any pending read to fail, ending the loop
        closeBuffers()

        // The socketThread loop will now clean up everything
    }

    fun writeln(str: String) {
        if (cancelled) {
            Log.d(Const.TAG, "Skipping write to cancelled socket: [$str]")
            return
        }
        Log.d(Const.TAG, "Writing to TCP socket: [$str]")
        try {
            bufferedWriter.write(str + "\n")
            bufferedWriter.flush()
        } catch (e: IOException) {
            Log.d(Const.TAG, "Failed to write [$str] to TCP socket with exception $e")
            stopListener()
        }
    }

    private fun closeBuffers() {
        // Call close on the top level buffers which will propagate to the original socket
        // and cause any pending reads and writes to fail
        if (::bufferedWriter.isInitialized) {
            try {
                Log.d(Const.TAG, "Closing bufferedWriter")
                bufferedWriter.close()
            } catch (e: IOException) {
                Log.d(Const.TAG, "Closing bufferedWriter in stopListener caused IOException, this is probably ok")
            }
        }
        if (::dataInputStream.isInitialized) {
            try {
                Log.d(Const.TAG, "Closing dataInputStream")
                dataInputStream.close()
            } catch (e: IOException) {
                Log.d(Const.TAG, "Closing dataInputStream in stopListener caused IOException, this is probably ok")
            }
        }
    }

    // In a separate function so we can "return" any time to bail out
    private fun socketThread() {
        try {
            socket = Socket(address, port)
        } catch (e: Exception) {
            Log.e(Const.TAG, "Failed to connect to $address:$port with exception $e")
            MainActivity.doUiThread { callback.onDisconnectTCP(null,this) }
            return
        }

        // Wrap the socket up so we can work with it - no exceptions should be thrown here
        try {
            outputStreamWriter = OutputStreamWriter(socket.getOutputStream())
            bufferedWriter = BufferedWriter(outputStreamWriter)
            dataInputStream = DataInputStream(socket.getInputStream())
        } catch (e: IOException) {
            Log.e(Const.TAG, "Exception while opening socket buffers $e")
            closeBuffers()
            MainActivity.doUiThread { callback.onDisconnectTCP(null,this) }
            return
        }

        // Connection should be established, everything is ready to read and write
        MainActivity.doUiThread { callback.onConnectTCP(this) }

        // There is always a header at the start before the PNG stream, read that first
        var header = ByteArray(Const.TCP_INTRO_HEADER)
        try {
            dataInputStream.readFully(header)
        } catch (e: IOException) {
            Log.d(Const.TAG, "Failed to receive initial header, connection has failed")
            cancelled = true
        }
        if (!cancelled) {
            MainActivity.doUiThread { callback.onReceiveTCPHeader(header, this) }
        }

        // Start reading from the socket, any writes happen from another thread
        var reason: String? = null
        while (!cancelled) {
            // Each window transmission starts with !_____X_ where X is a binary byte 0x00 to 0xFF
            var windowId: Int = -1
            var expectedBytes: Int = -1
            try {
                val a = dataInputStream.readByte().toChar()
                val b = dataInputStream.readByte().toChar()
                val c = dataInputStream.readByte().toChar()
                val d = dataInputStream.readByte().toChar()
                val e = dataInputStream.readByte().toChar()
                val f = dataInputStream.readByte().toChar()
                windowId = dataInputStream.readByte().toInt()
                val h = dataInputStream.readByte().toChar()
                if ((a != '!') || (b != '_') || (c != '_') || (d != '_') || (e != '_') || (f != '_') || (h != '_')) {
                    reason = "Image header invalid ![$a] _[$b] _[$c] _[$d] _[$e] _[$f] W[$windowId] _[$h]"
                    cancelled = true
                    break
                }
                // Read the number of upcoming PNG bytes
                val b0 = dataInputStream.readByte().toInt() and 0xFF
                val b1 = dataInputStream.readByte().toInt() and 0xFF
                val b2 = dataInputStream.readByte().toInt() and 0xFF
                val b3 = dataInputStream.readByte().toInt() and 0xFF
                expectedBytes = (((((b3 * 256) + b2) * 256) + b1) * 256) + b0
                // Log.d(Const.TAG, "Found header for window $windowId with $expectedBytes bytes of PNG data (lsb)b0=$b0, b1=$b1, b2=$b2, (msb)b3=$b3")

                val w = dataInputStream.readByte().toChar()
                val x = dataInputStream.readByte().toChar()
                val y = dataInputStream.readByte().toChar()
                val z = dataInputStream.readByte().toChar()
                if ((w != '_') || (x != '_') || (y != '_') || (z != '_')) {
                    reason = "Image second header invalid _[$w] _[$x] _[$y] _[$z]"
                    cancelled = true
                    break
                }
            } catch (e: IOException) {
                Log.d(Const.TAG, "Failed to receive window header, connection has failed")
                cancelled = true
                break
            }

            if ((windowId == callback.getWindow1Index()) || (windowId == callback.getWindow2Index())) {
                // Read the raw PNG data and decode it
                var bitmap: Bitmap?
                try {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
                        // The image decoder in >= API 19 correctly reads all the bytes for the image
                        bitmap = BitmapFactory.decodeStream(dataInputStream)
                    } else {
                        // Android versions earlier than API 19 have a bug where decodeStream does not read up all
                        // remaining bytes and the stream gets out of sync. So we read the data manually into a
                        // buffer and then pass it in for decoding. If I could measure the bytes read in decodeStream
                        // we could do this more efficiently.
                        val pngData = ByteArray(expectedBytes)
                        dataInputStream.readFully(pngData)
                        val byteArrayInputStream = ByteArrayInputStream(pngData)
                        bitmap = BitmapFactory.decodeStream(byteArrayInputStream)
                    }
                } catch (e: IOException) {
                    Log.d(Const.TAG, "Exception during socket read or bitmap decode $e")
                    bitmap = null
                }
                if (bitmap == null) {
                    Log.d(Const.TAG, "Bitmap decode returned null, connection has failed")
                    cancelled = true
                    reason = "Bitmap decode failure"
                    break
                } else {
                    MainActivity.doUiThread { callback.onReceiveTCPBitmap(windowId, bitmap, this) }
                }
            } else {
                // Read up the PNG data and ignore it
                try {
                    dataInputStream.skipBytes(expectedBytes)
                } catch (e: IOException) {
                    Log.d(Const.TAG, "Failure during skipping PNG receive, connection has failed")
                    cancelled = true
                    break
                }
            }

            // Read the ending padding nulls to 1024 bytes
            try {
                val skip = 1024 - (expectedBytes % 1024)
                // Log.d(Const.TAG, "Skipping $skip bytes to pad to 1024 bytes")
                dataInputStream.skipBytes(skip)
            } catch (e: IOException) {
                Log.d(Const.TAG, "Failure during padding receive, connection has failed")
                cancelled = true
                break
            }
        }

        // Close any outer buffers we own, which will propagate to the original socket
        closeBuffers()

        // The connection is gone, tell the listener in case they need to update the UI
        MainActivity.doUiThread { callback.onDisconnectTCP(reason, this) }
    }

    // Constructor starts a new thread to handle the blocking outbound connection
    init {
        Log.d(Const.TAG, "Created thread to connect to $address:$port")
        thread(start = true) {
            socketThread()
        }
    }
}
