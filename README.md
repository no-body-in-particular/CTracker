# CTracker
 Lightweight GPS tracker server - As seen on https://coredump.ws or in this talk: https://www.youtube.com/watch?v=jJehqls7yxM
 
 This server was built originally to support the JIMI AM01 ankle monitor, before traccar supported this device. It has a few workaround for the quirks of it, and is really lightweight to run ( I run it on a budget VPS myself ) - taking less than 3gb of ram - very little bandwidth and computing power. It's web-UI is also optimized for mobile and low power devices ( and for ease of use ).
 
 Protocol/device manuals are included - as well as the c-based server ( device_server folder ).
 The server can be installed by running "make && make install" in the device_server folder. Then the application should be run under the webserver user, with for instance "nohup su -c jimitrack hiawatha" for the user hiawatha.
 The /var/gps folder has a script to fetch the cell phone tower database - since GitHub isn't too great with large files ( it's 500mb ).
 You don't *need* to run this for the server to work, however it's a really nice speedup to have.
 
 The website that shows the data is contained in the /var/www folder. Both /var/www and /var/gps should be chown'd to your webserver's user.
 
 A default google API key is included for your (ab)use - and there are some sensible defaults set in "config.h" for file sizes on the device server - you should check yourself if these will work for you. 

 Contains icons from FontAwesome ( https://fontawesome.com/ ), a modified ( minimalized ) version of GregWar's captcha ( https://github.com/Gregwar/CaptchaBundle ), and a map by OpenLayers ( https://openlayers.org/ ). JQuery ( https://jquery.com/ ) is used, and JQuery Timepicker is also used for backwards browser compatibility (https://timepicker.co/#) . All with their respective licenses.
 
 I'm aware the c-based server code could use some cleanup, maybe i'll get to that.. sometime :P
 
 My code however, is GPL'd.
