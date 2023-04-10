# CTracker
 Lightweight GPS tracker server - this server was built originally to support the JIMI AM01 ankle monitor, before traccar supported this device. It has a few workaround for the quirks of it, and is really lightweight to run ( I run it on a budget VPS myself ) - taking less than 3gb of ram - very little bandwidth and computing power. It's web-UI is also optimized for mobile and low power devices ( and for ease of use ).
 
 Protocol/device manuals are included - as well as the c-based server ( device_server folder ).
 The server can be installed by running "make && make install" in the device_server folder. Then the application should be run under the webserver user, with for instance "nohup su -c jimitrack hiawatha" for the user hiawatha.
 The website that shows the data is contained in the var folder, there is also a /var/gps directory this contains all the databases necessary to run the webserver. This includes a mostly complete database of worldwide LBS stations for quick lookup ( nanoseconds ) .
 
 A default google API key is included for your (ab)use - and there are some sensible defaults set in "config.h" for file sizes on the device server - you should check yourself if these will work for you. 

 Contains icons from FontAwesome ( https://fontawesome.com/ ), a modified ( minimalized ) version of GregWar's captcha ( https://github.com/Gregwar/CaptchaBundle ), and a map by OpenLayers ( https://openlayers.org/ ). JQuery ( https://jquery.com/ ) is used, and JQuery Timepicker is also used for backwards browser compatibility (https://timepicker.co/#) . All with their respective licenses.
 
 I'm aware the c-based server code could use some cleanup, maybe i'll get to that.. sometime :P
 
 My code however, is GPL'd.
