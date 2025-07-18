import time
import pygame
from _thread import *
import threading
import socket
import io
import sys
from PIL import Image

#Put your HOSTs IP here
HOST = ""
TCP_PORT = 52500

#"Internal" stuff
TCP_INTRO_HEADER = 4096
TCP_PLUGIN_VERSION = "XTEv3"

windowActive = [0]
windowNames = []
bufferedReader = None

screenPos = [(0, 0)]

newPngData = []
newPngDataSync = []

#newPngData1 = None
#newPngData1Sync = threading.Lock()
#newPngData2 = None
#newPngData2Sync = threading.Lock()

def networkThread(hostname):
    while True:
        try:
            networkLoop(hostname)
        except Exception as e:
            print(f"Failed during network process - {e}")
        print("Network Process ended, Restart in 5 sec")
        time.sleep(5)




def networkLoop(hostname):
    global windowActive
    global newPngData
    global newPngDataSync 

    cancelled = False

    print(f"Connecting to {hostname} port {TCP_PORT}")
    try:
        while True:
            try:
                client =  socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                client.connect((hostname, TCP_PORT))
            except Exception as e:
                print(e)                
                print("No Connection, retry in 5 sec")
                time.sleep(5)
            else:
                break
            
        print("Connection established")

        # There is always a header at the start before the PNG stream, read that first
        if not cancelled:
            header = client.recv(TCP_INTRO_HEADER)
            headerStr = header.decode('utf-8', errors='ignore').split('\0', 1)[0]
            print(f"Received raw header [{headerStr}] before PNG stream")
            headerStream = io.StringIO(header.decode('utf-8', errors='ignore'))
            version = headerStream.readline().split(" ")[0]
            windowAircraft = headerStream.readline()
            texture = headerStream.readline().split(" ")
            textureWidth = int(texture[0])
            textureHeight = int(texture[1])
            print(f"Plugin version [{version}], aircraft [{windowAircraft}], texture {textureWidth}x{textureHeight}")
            line = None
            windowNames = []
            while True:
                line = headerStream.readline()
                if line is None or "__EOF__" in line:
                    break
                window = line.split(" ")
                name = window[0]
                windowNames.append(name)
                l, t, r, b = int(window[1]), int(window[2]), int(window[3]), int(window[4])
                print(f"Window [{name}] = ({l},{t})->({r},{b})")

            if version != TCP_PLUGIN_VERSION:
                print(f"Version [{version}] is not expected [{TCP_PLUGIN_VERSION}]")
                sys.exit(1)
            if len(windowNames) <= 0:
                print("No valid windows were sent")
                sys.exit(1)
    except Exception as e:
        print(f"Failed to receive initial header, connection has failed - {e}")

    while not cancelled:
        # Each window transmission starts with !_____X_ where X is a binary byte 0x00 to 0xFF
        windowId = -1
        expectedBytes = -1
        try:
            # We sometimes end up with PNG data, run a sliding window until we find the next header
            #a = client.recv(1).decode('utf-8', errors='ignore')
#while searching: 
            a = ''
            searching = True
            while searching: 
                dat = chr(ord(client.recv(1)))
                if(dat == '!'):
                    a = dat
                    searching = False

            #a = chr(ord(client.recv(1)))
            b = chr(ord(client.recv(1)))
            c = chr(ord(client.recv(1)))
            d = chr(ord(client.recv(1)))
            e = chr(ord(client.recv(1)))
            f = chr(ord(client.recv(1)))
            g = chr(ord(client.recv(1)))
            h = chr(ord(client.recv(1)))

            if a != '!' or b != '_' or c != '_' or d != '_' or e != '_' or f != '_' or h != '_':
                print(f"Image header invalid ![{a}] _[{b}] _[{c}] _[{d}] _[{e}] _[{f}] W[{windowId}] _[{h}]")
                #sys.exit(1)
                break
            #else :
            #    print(f"Image header ![{a}] _[{b}] _[{c}] _[{d}] _[{e}] _[{f}] W[{windowId}] _[{h}]")


            windowId = ord(g)

            # Read the number of upcoming PNG bytes
            b0 = ord(client.recv(1))&0xFF
            b1 = ord(client.recv(1))&0xFF
            b2 = ord(client.recv(1))&0xFF
            b3 = ord(client.recv(1))&0xFF
            expectedBytes = (((((b3 * 256) + b2) * 256) + b1) * 256) + b0

            w = chr(ord(client.recv(1)))
            x = chr(ord(client.recv(1)))
            y = chr(ord(client.recv(1)))
            z = chr(ord(client.recv(1)))

            if w != '_' or x != '_' or y != '_' or z != '_':
                print(f"Image second header invalid _[{w}] _[{x}] _[{y}] _[{z}]")
                break
            #else:
            #    print(f"wxyz {w} {x} {y} {z}")
        except Exception as e:
            print("Failed to receive window header, connection has failed")
            break

        try:
            #if windowId in windowActive or newPngData1 is not None:
            if not(windowId in windowActive): #or newPngData1 is not None:
                # Skip the PNG data if the window is not active or if we are still processing a previous image
                #client.recv(expectedBytes)
                recvall(client,expectedBytes)
            elif (windowId in windowActive):
                x = windowActive.index(windowId)
                # Read the expected PNG data into a buffer
                pngData = recvall(client,expectedBytes)
                # Pass the PNG data over to the display thread
                dataSync = newPngDataSync[x]
                newPngDataSync[x].acquire()
                newPngData[x] = pngData
                newPngDataSync[x].release()
            # Read the ending padding nulls to 1024 bytes
            try:
                skip = 1024 - (expectedBytes % 1024)
                # Log.d(Const.TAG, "Skipping " + skip + " bytes to pad to 1024 bytes")
                client.recv(skip)
            except Exception as e:
                print(f"Failure during padding receive, connection has failed - {e}")
                sys.exit(1)
        except Exception as e:
            print(f"Failed to read and decode image - {e}")
            #sys.exit(1)
            break

    print("Network loop ended")
    client.shutdown(socket.SHUT_RDWR)
    client.close()

# Define the constants TCP_PORT, TCP_INTRO_HEADER, TCP_PLUGIN_VERSION, windowActive, windowNames, newPngData, newPngDataSync, and windowAircraft before calling the networkLoop function.

def recvall(sock, n):
    # Helper function to recv n bytes or return None if EOF is hit
    data = bytearray()
    while len(data) < n:
        packet = sock.recv(n - len(data))
        if not packet:
            return None
        data.extend(packet)
    return data

def pilImageToSurface(pilImage):
    return pygame.image.fromstring(
        pilImage.tobytes(), pilImage.size, pilImage.mode).convert()


def Main():

    global newPngData
    global newPngDataSync 

    for x in range(len(windowActive)):
        newPngData.append(None)
        newPngDataSync.append(threading.Lock())

    # pygame setup
    pygame.init()
    
    screen = pygame.display.set_mode((0, 0),pygame.FULLSCREEN)
    clock = pygame.time.Clock()
    running = True
    dt = 0

    #Network Loop will be done in a separate Thread
    thread = threading.Thread(target=networkThread, args=(HOST,))
    thread.start()


    while running:
        #Polling Events
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False

        for x in range(len(windowActive)):        
            newData = newPngData[x]
            newDataSync = newPngDataSync[x]
            if(newData != None and len(newData)>0):
                try:
                    newDataSync.acquire()
                    cpNewPngData = bytearray(len(newData))
                    cpNewPngData[:] = newData
                    newDataSync.release()

                    _image = Image.open(io.BytesIO(cpNewPngData))
                    #Resize if required
                    #_image = _image.resize((800,480))
                    imp = pilImageToSurface(_image)
                    screen.blit(imp, screenPos[x])

                except Exception as e:
                    print(f"Failed to show  image - {e}")
                    pygame.draw.circle(screen, (255,0,0), screenPos[x], 5)            
            else:
                pygame.draw.circle(screen, (0,0,255), screenPos[x], 5)


        # flip() the display to put your work on screen
        pygame.display.flip()

        # limits FPS to 60
        # dt is delta time in seconds since last frame, used for framerate-
        # independent physics.
        dt = clock.tick(60) / 1000

    pygame.quit()

if __name__ == '__main__':
    Main()
