# FermWatch
Monitoring fermentation with Brewfather®, BrewPiLess and iSpindel or "No phone No worries!"

![bpl screen IMG_1322ssm](https://user-images.githubusercontent.com/38124525/164382814-4af067e7-b47f-446a-a56a-18721918490c.JPG) ![isp screen IMG_1320sm](https://user-images.githubusercontent.com/38124525/164383131-8af7bd72-f092-484e-ae2c-08549dafd988.JPG)

Have you struggled to see the BPL SSD1306 OLED screen?
Don’t have your phone handy?
Forgot your glasses?
Just want to see where your latest brew is up to?

The video Tutorial has step by step instructions and describes how to make a FermWatch with a D1 Mini and a 2.8" TFT ILI9341 to monitor your fermentation – in the shed, beside the bed, wherever your local network extends!

  
Link to video:   

**Important** I am a brewer and author, I write books on recreating historic beers - **NOT a PROGRAMMER** - as will be obvious from inspection of the code.
# Project
The FermWatch project was very much inspired by ZeSlammy’s iSpindHub project where the iSpindel readout was displayed on a small tft screen. https://github.com/ZeSlammy/iSpindHub

Initially, I just wanted a bigger screen for my BrewPiLess, but interfacing the ILI9341 proved to be too hard for me. Then I developed FermWatch as a standalone unit  to show BrewPiLess data and iSpindel data from Brewfather plus some derived functions. 
# Software issues and change requests
Please do not expect any form of support. I am a brewer not a coder/programmer! It has taken me 4 months to reach this stage where I think that it is working and stable enough to release. It works with the hardware, as described – it may not work with your parts/setup! 
# Main Prerequisites
- D1 Mini 8266 with 2.8" TFT ILI9341 320 * 240 (lots of these out there just don't buy a shield!) NB This project does not work with an ESP32!
- BrewPiLess System [BPL] Software v4.1r4 by Vitotai  https://github.com/vitotai/BrewPiLess
- iSpindel [iSp] Software v7.1.0 https://github.com/universam1/iSpindel    Cherry Philip board v4.0 Build your own https://youtu.be/4HYzm0psaNw
- Brewfather® v2.8.1 [BF]  https://brewfather.app/ Premium Version - so yes you have to pay an annual subscription but it is worth every cent.
# System Overview
![FW Overview 2022-04-21_16-09-55sm](https://user-images.githubusercontent.com/38124525/164385886-e3cf825e-5781-480c-b466-e32460241675.png)

Key parts
- iSp set to send data to BF
- FermWatch acts as a MQTT Broker to BPL - and pushes data every 75 - 115 secs
- Getting the current data from the BF API is a 2 step process the first request is a single shot to get the fermenting batch id, recipe name, measured OG and estimated FG
- The second request uses the batch id to get the batch iSpindel last readings  set for a 3 minute cycle, primarily to rotate the display  given the BF update cycle is 15 minutes
- the display screens then alternate between BPL and iSp
- Displays derived % ABV using the UK Tax Office adjustment factors cl 30.2 and 30.3 https://www.gov.uk/government/publications/excise-notice-226-beer-duty/excise-notice-226-beer-duty--2#calculation-strength 
# Options
- Select Plato - default SG
- Select Fahrenheit  - default Celsius
- Select iSpindel SG 20°C approximate temperature correction - default none. 	Which modifies the displayed Present 	Gravity, Apparent Attenuation and %  	ABV values

# Acknowledgements
This project would not have been possible without using libraries from Bodmer - TFT_eSPI screen, martin-ger - MQTT Broker, B Blanchon - ArduinoJson, tzapu - WiFiManger, NTPClient and the MultiMap for interpolation and others who are referenced in the code.

► I have achieved what I set out to do for the project, some compromises were made along the way, notably failing to get custom fonts to work but no worries.

My YouTube channel, Buy Books, GitHub and website:  
\------------------------------------------------------------------  
- https://m.youtube.com/channel/UCRhjjWS5IFHzldBhO2kyVkw/featured   Tritun Books Channel
- https://www.lulu.com/spotlight/prsymons  Books available Print on demand from Lulu
- https://github.com/Hwerow  This 
- https://prstemp.wixsite.com/tritun-books   Website  
\------------------------------------------------------------------
