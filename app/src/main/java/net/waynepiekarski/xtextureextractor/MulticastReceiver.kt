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

import android.util.Log

import java.io.IOException
import java.util.Arrays

import java.net.*
import kotlin.concurrent.thread


class MulticastReceiver (private var address: String, private var port: Int, private var callback: OnReceiveMulticast) {
    private lateinit var socket: MulticastSocket
    @Volatile private var cancelled = false
    private var lastAddress: InetAddress? = null
    private val timeoutLimitSeconds = 5 // Number of seconds before we give up and restart the socket

    interface OnReceiveMulticast {
        fun onReceiveMulticast(buffer: ByteArray, source: InetAddress, ref: MulticastReceiver)
        fun onFailureMulticast(ref: MulticastReceiver)
        fun onTimeoutMulticast(ref: MulticastReceiver)
    }

    init {
        Log.d(Const.TAG, "Created thread to listen on multicast address $address on port $port")
        thread(start = true) {
            while(!cancelled) {
                var packetCount = 0
                var timeoutCount = 0
                Log.d(Const.TAG, "Requesting multicast packets for address $address on port $port")
                try {
                    socket = MulticastSocket(port)
                    // Only block for 1 second before trying again, allows us to check for if cancelled
                    socket.soTimeout = 1000
                    socket.joinGroup(InetAddress.getByName(address))
                    Log.d(Const.TAG, "Ready for multicast packets for address $address on port $port")

                    val buffer = ByteArray(64 * 1024) // UDP maximum is 64kb
                    val packet = DatagramPacket(buffer, buffer.size)
                    while (!cancelled) {
                        // Log.d(Const.TAG, "Waiting for multicast packet on port " + port + " with maximum size " + buffer.size);
                        try {
                            socket.receive(packet)
                            packetCount++
                            Log.d(Const.TAG, "Received multicast packet with " + packet.length + " bytes of data");
                            // Log.d(Const.TAG, "Hex dump = [" + bytesToHex(packet.getData(), packet.getLength()) + "]");
                            // Log.d(Const.TAG, "Txt dump = [" + UDPReceiver.bytesToChars(packet.getData(), packet.getLength()) + "]");
                            // getHostAddress appears to block and should not be called on the UI thread!
                            if (lastAddress == null || !lastAddress!!.equals(packet.address)) {
                                lastAddress = packet.address
                                val copyAddress = packet.address // This uses a mutex and cannot be done within a UI handler
                                val copyData = Arrays.copyOfRange(buffer, 0, packet.length)
                                MainActivity.doUiThread { callback.onReceiveMulticast(copyData, copyAddress, this) }
                            }
                        } catch (e: SocketTimeoutException) {
                            timeoutCount++
                            if (timeoutCount >= timeoutLimitSeconds) {
                                Log.d(Const.TAG, "Multicast socket has not received anything in $timeoutLimitSeconds seconds, breaking out of socket loop")
                                break
                            } else {
                                Log.d(Const.TAG, "Multicast timeout $timeoutCount sec of $timeoutLimitSeconds sec, reading again ...")
                            }
                        } catch (e: IOException) {
                            Log.e(Const.TAG, "Failed to read packet, breaking out of socket loop: " + e)
                            break
                        } catch (e: Exception) {
                            Log.e(Const.TAG, "Unknown exception, breaking out of socket loop: " + e)
                            break
                        }
                    }
                    Log.d(Const.TAG, "Socket while loop escaped, closing down multicast listener on port " + port)
                    socket.close()
                } catch (e: SocketException) {
                    Log.e(Const.TAG, "Failed to open socket " + e)
                }

                if (!cancelled) {
                    Log.d(Const.TAG, "Socket has failed but not cancelled, so sleeping and trying again")
                    if (timeoutCount == 0)
                        MainActivity.doUiThread { callback.onFailureMulticast(this) }
                    else
                        MainActivity.doUiThread { callback.onTimeoutMulticast(this) }
                    Thread.sleep(1000)
                }
            }

            Log.d(Const.TAG, "Multicast listener thread for port $port has ended")
        }
    }

    fun stopListener() {
        Log.d(Const.TAG, "Stopping multicast listener and closing socket")
        cancelled = true
        if (::socket.isInitialized) {
            try {
                socket.close()
            } catch (e: IOException) {
                Log.d(Const.TAG, "Closing multicast socket in stopListener caused IOException, this is probably ok")
            }
        }
    }
}
