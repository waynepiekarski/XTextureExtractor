// ---------------------------------------------------------------------
//
// XTextureExtractor
//
// Copyright (C) 2018-2022 Wayne Piekarski
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

import android.app.Activity
import android.content.res.Configuration
import android.graphics.*
import android.util.Log
import android.view.View
import android.widget.Toast
import kotlinx.android.synthetic.main.activity_main.*
import java.net.InetAddress
import android.widget.EditText
import android.app.AlertDialog
import android.content.Context
import android.os.*
import android.text.InputType
import android.view.KeyEvent
import android.widget.LinearLayout
import java.io.BufferedReader
import java.io.ByteArrayInputStream
import java.io.IOException
import java.io.InputStreamReader
import java.net.UnknownHostException


open class MainActivity : Activity(), TCPBitmapClient.OnTCPBitmapEvent, MulticastReceiver.OnReceiveMulticast {

    private var becn_listener: MulticastReceiver? = null
    private var tcp_extplane: TCPBitmapClient? = null
    private var connectAddress: String? = null
    private var manualAddress: String = ""
    private var manualInetAddress: InetAddress? = null
    private var connectAircraft: String = ""
    private var connectWorking = false
    private var connectShutdown = false
    private var connectFailures = 0
    private var lastLayoutLeft   = -1
    private var lastLayoutTop    = -1
    private var lastLayoutRight  = -1
    private var lastLayoutBottom = -1
    private var windowNames: ArrayList<String> = ArrayList(0)
    private var window1Idx = -1
    private var window2Idx = -1

    override fun onCreate(savedInstanceState: Bundle?) {
        Log.d(Const.TAG, "onCreate()")
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // Reset the layout cache, the variables could be kept around after the Activity is restarted
        lastLayoutLeft   = -1
        lastLayoutTop    = -1
        lastLayoutRight  = -1
        lastLayoutBottom = -1

        // Add the compiled-in BuildConfig values to the about text
        aboutText.text = aboutText.getText().toString().replace("__VERSION__", "Version: " + Const.getBuildVersion() + " " + BuildConfig.BUILD_TYPE + " build " + Const.getBuildId() + " " + "\nBuild date: " + Const.getBuildDateTime())

        // Reset the display to an empty state with no images
        resetDisplay()
        Toast.makeText(this, R.string.help_text, Toast.LENGTH_LONG).show()

        // Miscellaneous counters that also need reset
        connectFailures = 0

        for (clickTarget in arrayOf(aboutTarget1, aboutTarget2, aboutTarget3, aboutTarget4)) {
            clickTarget.setOnClickListener {
                // Toggle the help text
                if (aboutText.visibility == View.VISIBLE) {
                    aboutText.visibility = View.INVISIBLE
                } else {
                    aboutText.visibility = View.VISIBLE
                }
            }
        }

        textureImage1.setOnClickListener { changeWindow1() }

        textureImage2.setOnClickListener { changeWindow2() }

        connectText.setOnClickListener {
            popupManualHostname()
        }

        aboutText.setOnClickListener {
            aboutText.visibility = View.INVISIBLE
        }
    }

    fun changeWindow1() {
        if (windowNames.size <= 0)
            return
        window1Idx++
        if (window1Idx >= windowNames.size)
            window1Idx = 0
        val sharedPref = getPreferences(Context.MODE_PRIVATE)
        with(sharedPref.edit()){
            putInt("window_1_idx", window1Idx)
            commit()
        }
        Log.d(Const.TAG, "Changed window for texture 1 to $window1Idx=[${windowNames[window1Idx]}]")
    }

    fun changeWindow2() {
        if (windowNames.size <= 0)
            return
        window2Idx++
        if (window2Idx >= windowNames.size)
            window2Idx = 0
        val sharedPref = getPreferences(Context.MODE_PRIVATE)
        with(sharedPref.edit()){
            putInt("window_2_idx", window2Idx)
            commit()
        }
        Log.d(Const.TAG, "Changed window for texture 2 to $window2Idx=[${windowNames[window2Idx]}]")
    }

    // Handle D-pad events to change the windows
    override fun onKeyDown(keyCode: Int, ev: KeyEvent): Boolean {
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_LEFT -> changeWindow1()
            KeyEvent.KEYCODE_DPAD_RIGHT -> changeWindow2()
            KeyEvent.KEYCODE_DPAD_UP -> aboutText.visibility = if (aboutText.visibility == View.VISIBLE) View.INVISIBLE else View.VISIBLE
            KeyEvent.KEYCODE_DPAD_DOWN -> aboutText.visibility = if (aboutText.visibility == View.VISIBLE) View.INVISIBLE else View.VISIBLE
            else -> return@onKeyDown super.onKeyDown(keyCode, ev) // Allow unknown events to be processed
        }
        return true // We processed the event here, do not continue
    }


    companion object {
        private var backgroundThread: HandlerThread? = null

        fun doUiThread(code: () -> Unit) {
            Handler(Looper.getMainLooper()).post { code() }
        }

        fun doBgThread(code: () -> Unit) {
            Handler(backgroundThread!!.getLooper()).post { code() }
        }
    }

    override fun getWindow1Index(): Int { return window1Idx }
    override fun getWindow2Index(): Int { return window2Idx }

    // The user can click on the connectText and specify a X-Plane hostname manually
    private fun changeManualHostname(hostname: String) {
        if (hostname.isEmpty()) {
            Log.d(Const.TAG, "Clearing override X-Plane hostname for automatic mode, saving to prefs, restarting networking")
            manualAddress = hostname
            val sharedPref = getPreferences(Context.MODE_PRIVATE)
            with(sharedPref.edit()){
                putString("manual_address", manualAddress)
                commit()
            }
            restartNetworking()
        } else {
            Log.d(Const.TAG, "Setting override X-Plane hostname to $hostname")
            // Lookup the IP address on a background thread
            doBgThread {
                try {
                    manualInetAddress = InetAddress.getByName(hostname)
                } catch (e: UnknownHostException) {
                    // IP address was not valid, so ask for another one and exit this thread
                    doUiThread { popupManualHostname(error=true) }
                    return@doBgThread
                }

                // We got a valid IP address, so we can now restart networking on the UI thread
                doUiThread {
                    manualAddress = hostname
                    Log.d(Const.TAG, "Converted manual X-Plane hostname [$manualAddress] to ${manualInetAddress}, saving to prefs, restarting networking")
                    val sharedPref = getPreferences(Context.MODE_PRIVATE)
                    with(sharedPref.edit()) {
                        putString("manual_address", manualAddress)
                        commit()
                    }
                    restartNetworking()
                }
            }
        }
    }

    private fun popupManualHostname(error: Boolean = false) {
        val builder = AlertDialog.Builder(this)
        if (error)
            builder.setTitle("Invalid entry! Specify X-Plane hostname or IP")
        else
            builder.setTitle("Specify X-Plane hostname or IP")

        val input = EditText(this)
        input.setText(manualAddress)
        input.setInputType(InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS)
        builder.setView(input)
        builder.setPositiveButton("Manual Override") { dialog, which -> changeManualHostname(input.text.toString()) }
        builder.setNegativeButton("Revert") { dialog, which -> dialog.cancel() }
        builder.setNeutralButton("Automatic Multicast") { dialog, which -> changeManualHostname("") }
        builder.show()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        // Only implement full-screen in API >= 19, older Android brings them back on each click
        if (hasFocus && Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            or View.SYSTEM_UI_FLAG_FULLSCREEN
                            or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
        }
    }

    override fun onConfigurationChanged(config: Configuration) {
        Log.d(Const.TAG, "onConfigurationChanged with layout change orientation=${config.orientation}")
        super.onConfigurationChanged(config)
        if (config.orientation == Configuration.ORIENTATION_PORTRAIT)
            textureLayout.setOrientation(LinearLayout.VERTICAL)
        else if (config.orientation == Configuration.ORIENTATION_LANDSCAPE)
            textureLayout.setOrientation(LinearLayout.HORIZONTAL)
        else
            Log.e(Const.TAG, "Unknown orientation value ${config.orientation}")
    }

    override fun onResume() {
        super.onResume()
        Log.d(Const.TAG, "onResume()")
        connectShutdown = false

        // Start up our background processing thread
        backgroundThread = HandlerThread("BackgroundThread")
        backgroundThread!!.start()

        // Retrieve the manual address from shared preferences
        val sharedPref = getPreferences(Context.MODE_PRIVATE)
        val prefAddress = sharedPref.getString("manual_address", "").orEmpty()
        window1Idx = sharedPref.getInt("window_1_idx", 0)
        window2Idx = sharedPref.getInt("window_2_idx", 1)
        Log.d(Const.TAG, "Found preferences value for manual_address = [$prefAddress]")

        val ori = getResources().getConfiguration().orientation
        Log.d(Const.TAG, "onResume detected orientation=$ori")
        if (ori == Configuration.ORIENTATION_PORTRAIT)
            textureLayout.setOrientation(LinearLayout.VERTICAL)
        else if (ori == Configuration.ORIENTATION_LANDSCAPE)
            textureLayout.setOrientation(LinearLayout.HORIZONTAL)
        else
            Log.e(Const.TAG, "Unknown orientation value $ori")

        // Pass on whatever this string is, and will end up calling restartNetworking()
        changeManualHostname(prefAddress)
    }

    private fun setConnectionStatus(line1: String, line2: String, fixup: String, dest: String? = null) {
        Log.d(Const.TAG, "Changing connection status to [$line1][$line2] with destination [$dest]")
        var out = line1 + ". "
        if (line2.length > 0)
            out += "${line2}. "
        if (fixup.length > 0)
            out += "${fixup}. "
        if (dest != null)
            out += "${dest}."
        if (connectFailures > 0)
            out += "\nError #$connectFailures"

        connectText.text = out
    }

    private fun resetDisplay() {
        textureImage1.setImageBitmap(Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888))
        textureImage2.setImageBitmap(Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888))
    }

    private fun restartNetworking() {
        Log.d(Const.TAG, "restartNetworking()")
        resetDisplay()
        setConnectionStatus("Closing down network", "", "Wait a few seconds")
        connectAddress = null
        connectWorking = false
        connectAircraft = ""
        if (tcp_extplane != null) {
            Log.d(Const.TAG, "Cleaning up any TCP connections")
            tcp_extplane!!.stopListener()
            tcp_extplane = null
        }
        if (becn_listener != null) {
            Log.w(Const.TAG, "Cleaning up the BECN listener, somehow it is still around?")
            becn_listener!!.stopListener()
            becn_listener = null
        }
        if (connectShutdown) {
            Log.d(Const.TAG, "Will not restart BECN listener since connectShutdown is set")
        } else {
            if (manualAddress.isEmpty()) {
                setConnectionStatus("Waiting for X-Plane", "BECN broadcast", "Touch to override")
                Log.d(Const.TAG, "Starting X-Plane BECN listener since connectShutdown is not set")
                becn_listener = MulticastReceiver(Const.BECN_ADDRESS, Const.BECN_PORT, this)
            } else {
                Log.d(Const.TAG, "Manual address $manualAddress specified, skipping any auto-detection")
                check(tcp_extplane == null)
                connectAddress = manualAddress
                setConnectionStatus("Manual TCP connect", "Connection in progress", "Needs XTextureExtractor plugin", "$connectAddress:${Const.TCP_PLUGIN_PORT}")
                tcp_extplane = TCPBitmapClient(manualInetAddress!!, Const.TCP_PLUGIN_PORT, this)
            }
        }
    }

    override fun onPause() {
        Log.d(Const.TAG, "onPause()")
        connectShutdown = true // Prevent new BECN listeners starting up in restartNetworking
        if (tcp_extplane != null) {
            Log.d(Const.TAG, "onPause(): Cancelling existing TCP connection")
            tcp_extplane!!.stopListener()
            tcp_extplane = null
        }
        if (becn_listener != null) {
            Log.d(Const.TAG, "onPause(): Cancelling existing BECN listener")
            becn_listener!!.stopListener()
            becn_listener = null
        }
        backgroundThread!!.quit()
        super.onPause()
    }

    override fun onDestroy() {
        Log.d(Const.TAG, "onDestroy()")
        super.onDestroy()
    }

    override fun onFailureMulticast(ref: MulticastReceiver) {
        if (ref != becn_listener)
            return
        connectFailures++
        setConnectionStatus("No network available", "Cannot listen for BECN", "Enable WiFi")
    }

    override fun onTimeoutMulticast(ref: MulticastReceiver) {
        if (ref != becn_listener)
            return
        Log.d(Const.TAG, "Received indication the multicast socket is not getting replies, will restart it and wait again")
        connectFailures++
        setConnectionStatus("Timeout waiting for", "BECN multicast", "Touch to override")
    }

    override fun onReceiveMulticast(buffer: ByteArray, source: InetAddress, ref: MulticastReceiver) {
        if (ref != becn_listener)
            return
        setConnectionStatus("Found BECN multicast", "", "Wait a few seconds", source.getHostAddress())
        connectAddress = source.toString().replace("/","")

        // The BECN listener will only reply once, so close it down and open the TCP connection
        becn_listener!!.stopListener()
        becn_listener = null

        check(tcp_extplane == null)
        Log.d(Const.TAG, "Making connection to $connectAddress:${Const.TCP_PLUGIN_PORT}")
        tcp_extplane = TCPBitmapClient(source, Const.TCP_PLUGIN_PORT, this)
    }

    override fun onConnectTCP(tcpRef: TCPBitmapClient) {
        if (tcpRef != tcp_extplane)
            return
        // The socket isn't fully connected, so don't update the UI yet
    }

    override fun onDisconnectTCP(reason: String?, tcpRef: TCPBitmapClient) {
        if (tcpRef != tcp_extplane)
            return
        Log.d(Const.TAG, "onDisconnectTCP(): Closing down TCP connection and will restart")
        if (reason != null) {
            Log.d(Const.TAG, "Network failed due to reason [$reason]")
            Toast.makeText(this, "Network failed - $reason", Toast.LENGTH_LONG).show()
        }
        connectFailures++
        restartNetworking()
    }

    override fun onReceiveTCPBitmap(windowId: Int, image: Bitmap, tcpRef: TCPBitmapClient) {
        // If the current connection does not match the incoming reference, it is out of date and should be ignored.
        // This is important otherwise we will try to transmit on the wrong socket, fail, and then try to restart.
        if (tcpRef != tcp_extplane)
            return

        if (!connectWorking) {
            // Everything is working with actual data coming back.
            connectFailures = 0
            setConnectionStatus("XTextureExtractor", connectAircraft, "", "$connectAddress:${Const.TCP_PLUGIN_PORT}")
            connectWorking = true
        }

        // Store the image into the layout, which will resize it to fit the screen
        // Log.d(Const.TAG, "TCP returned window $windowId bitmap $image with ${image.width}x${image.height}, win1=$window1Idx, win2=$window2Idx")
        if (window1Idx == windowId)
            textureImage1.setImageBitmap(image)
        if (window2Idx == windowId)
            textureImage2.setImageBitmap(image)
    }

    private fun networkingFatal(reason: String) {
        Log.d(Const.TAG, "Network fatal error due to reason [$reason]")
        Toast.makeText(this, "Network error - $reason", Toast.LENGTH_LONG).show()
        restartNetworking()
    }

    override fun onReceiveTCPHeader(header: ByteArray, tcpRef: TCPBitmapClient) {
        var headerStr = String(header)
        headerStr = headerStr.substring(0, headerStr.indexOf(0x00.toChar()))
        Log.d(Const.TAG, "Received raw header [$headerStr] before PNG stream")
        try {
            val bufferedReader = BufferedReader(InputStreamReader(ByteArrayInputStream(header)))
            val version = bufferedReader.readLine().split(' ')[0]
            val aircraft = bufferedReader.readLine()
            val texture = bufferedReader.readLine().split(' ')
            val textureWidth = texture[0].toInt()
            val textureHeight = texture[1].toInt()
            Log.d(Const.TAG, "Plugin version [$version], aircraft [$aircraft], texture ${textureWidth}x${textureHeight}")
            var line: String?
            windowNames.clear()
            while (true) {
                line = bufferedReader.readLine()
                if (line == null || line.contains("__EOF__"))
                    break
                val window = line.split(' ')
                val name = window[0]
                windowNames.add(name)
                val l = window[1].toInt()
                val t = window[2].toInt()
                val r = window[3].toInt()
                val b = window[4].toInt()
                Log.d(Const.TAG, "Window [$name] = ($l,$t)->($r,$b)")
            }
            connectAircraft = "$aircraft ${textureWidth}x${textureHeight}"
            if (version != Const.TCP_PLUGIN_VERSION) {
                networkingFatal("Version [$version] is not expected [${Const.TCP_PLUGIN_VERSION}]")
                return
            }
            if (windowNames.size <= 0) {
                networkingFatal("No valid windows were sent")
                return
            }
            if (window1Idx >= windowNames.size) {
                window1Idx = 0
            }
            if (window2Idx >= windowNames.size) {
                if (windowNames.size > 1)
                    window2Idx = 1
                else
                    window2Idx = 0
            }
            val sharedPref = getPreferences(Context.MODE_PRIVATE)
            with(sharedPref.edit()){
                putInt("window_1_idx", window1Idx)
                putInt("window_2_idx", window2Idx)
                commit()
            }
        } catch (e: IOException) {
            Log.e(Const.TAG, "IOException processing header - $e")
            networkingFatal("Invalid header data")
            return
        } catch (e: Exception) {
            Log.e(Const.TAG, "Unknown exception processing header - $e")
            networkingFatal("Invalid header read")
            return
        }
    }
}
