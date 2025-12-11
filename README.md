# Automotive CAN Sniffer and Dashboard

Hello, this is Minjae Kim and this is my ECE 4180 final project report. Over the past month, I developed a CAN sniffer and dashboard that can connect to my car's OBD-II port, receive various vehicle telemetry, and display on a website hosted on the ESP32 via AP.

Before everything, here is a [Youtube video](https://youtu.be/3Cwpt5kMr2Q) demoing everything working.

(I couldn't show 0-60 time because I'm in a parking lot)

### Background

Almost all commercial cars have numerous ECUs scattered around, each with a specific purpose such as engine control or safety features. To communicate with each other, they use the CAN protocol, a serial communication standard known for its reliability and noise resiliency due to its error checking. Since these ECUs control basically everything about the car, you can get a lot of really cool information if you could read and decode these messages. That is what I planned to do. 

By connecting to the car's OBD-II port, which is normally used for diagnostics such as emissions testing, I could get access to a CANH and CANL line, and by using a CAN transceiver module (ESP32 does not have one built-in), I thought I could read those messages. 

However, even before the whole reverse engineering side of things, turns out I am not allowed to freely access all the CAN frames. The OBD-II port greatly filters the information I could access, which meant that unless I rip open my car and poke around wires, I can't get a lot of the cool data I was excited about.

In the end, I realized there were a lot of OBD-II diagnostics tools out there that do something really similar, so I tried to add some extra things like a website to see the readings in real time, and some extra features such as horesepower, aggressiveness (arbitrarily calculated) and 0-60 time. 

### Electronics

This is the simplest part of the project, as there is only one peripheral other than the ESP32 and my car.

The CAN transceiver I used was the MCP2551, which connects to my car via CANH and CANL and connects to the ESP32 via serial (TX and RX lines). Here is an image of my breadboard and the circuit diagram for better understanding.

![](/images/breadboard.png)

![](/images/circuitdiagram.png)

I had to use a voltage divider to lower the voltage coming from the CRX pin from 5V to 3.3V so the ESP32 could properly read the values.

The RS pin on the MCP2551 is connected to GND to enable the high speed mode.

### Code

I designed the FreeRTOS firmware with four tasks, which might seem unnecessary for such a simple project, but I liked how it made the development process easier. Also, it is better for the future if I wanated to add more functionalities if the system has clear separation of tasks.

Anyhow, here is what each task does in a nutshell:

- RequestTask: Sends requests to my car for each PID (RPM, speed, throttle, load)
- ReceiveTask: Receives CAN frames from my car and updates RawTelemetry struct
- ProcessTask: Reads the updated RawTelemetry struct and calculates more high-level diagnostics such as horsepower, aggressiveness, and 0-60 time and then updates ProcTelemetry
- WebsocketTask: Reads from the two structs and sends a JSON to update my web UI

Since there is a lot of producer-consumer action going on here, I made sure to use mutexes when needed to prevent data races. Combined with priority levels and yielding, I tried to prevent a lot of the issues that comes with RTOSes.

For the website, I used a very simple HTML/CSS/JS structure with the JavaScript file receiving JSON packets and updating the text. I'm not a webdev person so I didn't know how to make it prettier. I did make the aggressiveness bar change colors as the value goes up which is kind of cool.

The HTML/CSS/JS files are stored in the ESP32's SPIFFS partition, which is a feature that basically lets us use the flash memory to non-volatilely store files. This allows the system to be self-contained instead of needing internet connection or some external server. 

### Reflections

The main issue I had when developing was understanding how the MCP2551 worked. I didn't know the OBD-II port only sends telemetry when I send a request, so I spent a lot of time debugging by poking all around with a multimeter and even buying a different CAN transceiver. I was reluctant to send any messages from the ESP32 to my car because I read somewhere that it could mess up the car. It turnes out that newer cars are smart enough to filter anything that could be harmful.

This was a fun project, especially because I wanted to try using the CAN protocol for the longest time, but it was also pretty disappointing in the end. I was expecting to do some hardcore reverse engineering by reading individual bytes of thousands of seemingly random messaages, carefully pressing the pedal to match what I see on my dashboard with the sniffed data. If I had more time (and courage), I would like to get the raw CAN frames from internal lines and get some telemetry that commercial diagnostics tools can't provide. I also want to have some kind of output on my device, even an LED flashing when I am driving too aggressive would be cool.

### Credits

External libraries used:
[ArduinoJSON](https://arduinojson.org/?utm_source=meta&utm_medium=library.properties)
[ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer)

Plugins used:
[ESP32FS](https://github.com/me-no-dev/arduino-esp32fs-plugin)

Examples used:
[ESP32 Web Server using SPIFFS](https://randomnerdtutorials.com/esp32-web-server-spiffs-spi-flash-file-system/)
[ESP32 WebSocket Server](https://randomnerdtutorials.com/esp32-websocket-server-sensor/)
TWAItransmit and TWAIreceive.ino from driver/twai.h

Others:
[OBD-II PID table](https://www.csselectronics.com/pages/obd2-pid-table-on-board-diagnostics-j1979)