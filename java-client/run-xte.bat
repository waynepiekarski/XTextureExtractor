echo on
javac src/net/waynepiekarski/XTextureExtractor.java
if (%ERRORLEVEL% > 0) then (
  REM javac not found, you need to install JDK and it must be in your system PATH
  PAUSE
)
REM Replace %1-%9 with the IP address of your X-Plane instance, plus any extra flags
REM The syntax is: <hostname> [--fullscreen] [--windowN] [--geometry=X,Y,W,H]
REM An example would be to replace %1-%9 with this for window #1 at 50,50 with dimensions 512x512:
REM 192.168.99.88 --window1 --geometry=50,50,512,512
java -classpath src/ net.waynepiekarski.XTextureExtractor %1 %2 %3 %4 %5 %6 %7 %8 %9
if (%ERRORLEVEL% > 0) then (
  REM Failed to run client, check the error message for details
  PAUSE
)
