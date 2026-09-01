<?php
include_once 'lib.php';
include_once 'database.php';
validateSession();

?>
<!DOCTYPE html>
<html>
   <head>
      <!-- maximum-scale and user-scalable=0 used to be here. Blocking pinch zoom fails WCAG 1.4.4
           and is a poor fit for a map, where zooming in on a label is the whole point. -->
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <link rel="stylesheet" href="openlayers/ol.css" type="text/css">
      <link rel="stylesheet" href="style/tracker.css" type="text/css">
      <link rel="stylesheet" href="jquery/jquery-ui.css" type="text/css">
      <link rel="stylesheet" href="jquery/jquery.timepicker.min.css" type="text/css">
      <script src="jquery/jquery-3.7.1.js"></script>
      <script src="jquery/jquery-ui.js"></script>
      <!-- Both of these used to be fetched from cdnjs, so the page lost its time pickers and
           its graph anywhere that host is blocked or unreachable, and Chart.js 2.8.0 has been
           end of life since 2021. Served from here now, and the chart is on 4.x.
           v3 dropped the bundled moment.js, so a date adapter is a separate file - the graph
           has a time axis and will not build one without it. -->
      <script src="jquery/jquery.timepicker.min.js"></script>
      <script src="chartjs/chart.umd.min.js"></script>
      <script src="chartjs/chartjs-adapter-date-fns.bundle.min.js"></script>

      <script src="openlayers/ol.js"></script>
      <script id="assignments" type="text/javascript" >
         var defaultCenter=[52.0,5];
         var imei='';
      </script>
      <title>Gps tracker map</title>
   </head>
   <body>
      <div id="around" style="display: flex;flex-direction:row;width:100%;height:100%">
         <div id="aroundNav" align="left">
            <nav>
               <a href="#account" class="tooltip" aria-label="Account"> <span class="tooltiptext"  >Account</span><i class="icon user"></i></a>
               <a href="#history" class="tooltip" aria-label="Playback" ><span class="tooltiptext">Playback</span><i class="icon clock"></i></a>
               <a href="#stats" class="tooltip" aria-label="Statistics" ><span class="tooltiptext">Statistics</span><i class="icon chart-line"></i></a>
               <a href="#alarms" class="tooltip" aria-label="Alarms"><span class="tooltiptext">Alarms</span><i class="icon exclamation-triangle"></i></a>
               <a href="#geofence" class="tooltip" aria-label="Geofence"><span class="tooltiptext">Geofence</span><i class="icon map-marked-alt"></i></a>
               <a href="#commands" onClick="refreshCommandResults()" class="tooltip" aria-label="Commands"><span class="tooltiptext">Commands</span><i class="icon terminal"></i></a>
               <a href="#serverLogging" class="tooltip" aria-label="Server logging" ><span class="tooltiptext">Server logging</span><i class="icon file"></i></a>
               <a href="#trips" class="tooltip" aria-label="Trips"><span class="tooltiptext">Trips</span><i class="icon address-card"></i></a>
               <a href="#settings" class="tooltip" aria-label="Settings" ><span class="tooltiptext">Settings</span><i class="icon cog"></i></a>
               <a href="#" class="tooltip" aria-label="Close"><span class="tooltiptext">Close</span><i class="icon backward"></i></a>
            </nav>
         </div>
         <div class='container' align="left">
            <section id='account' align="left">
               <!-- grouped so they sit side by side: the panel is a flex column, so two loose
                    children stack one above the other -->
               <div class="panelActions">
                  <button onclick="window.location.href='logout.php'" class="button">log out</button>
                  <a href="#accountUpdate" class="button">change details</a>
               </div>
               <div class="table-scroll">
                  <table id="deviceTable"  class="table">
                     <thead>
                        <tr>
                           <th scope="col">IMEI</th>
                           <th scope="col">Name</th>
                           <th scope="col">Actions</th>
                        </tr>
                        <tr>
                           <th><input placeholder="IMEI" id="imei" aria-label="Device IMEI" name="imei" class="input" size="17"/></th>
                           <th><input placeholder="Name" id="name" name="name" class="input" size="17"/></th>
                           <th><button onclick="saveDevice()" class="button">Add</button></th>
                        </tr>
                     </thead>
                     <tbody id="deviceBody">
                     </tbody>
                  </table>
               </div>
            </section>
            <section id='accountUpdate' align="left">
               <input placeholder="Username" id="username" aria-label="Username" name="username" class="input"/>
               <input placeholder="Name" id="name_for_account" name="name_for_account" class="input"/>
               <input placeholder="Email" id="email" aria-label="Email address" name="email" class="input"/>
               <input placeholder="Password (blank = unchanged)" id="password" name="password" class="input" type="password" onfocus="this.value=''"/>
               <button onclick="updateUser()" class="button">Update</button>
            </section>
            <section id='history' align="left">
               <button type="button" onclick="playSlower()" class="playctl" aria-label="Slower"><i class="icon slower"></i></button>
               <button type="button" onclick="startPlaying()" class="playctl" aria-label="Play"><i class="icon play"></i></button>
               <button type="button" onclick="pausePlaying()" class="playctl" aria-label="Pause"><i class="icon pause"></i></button>
               <button type="button" onclick="stopPlaying()" class="playctl" aria-label="Stop"><i class="icon stop"></i></button>
               <button type="button" onclick="playFaster()" class="playctl" aria-label="Faster"><i class="icon faster"></i></button>
            </section>

            <section id='stats' align="left">
               <div id="statsControls" class="chips"></div>
               <!-- the canvas is sized by this wrapper: Chart.js overrides a height set on the
                    canvas itself, which is why the graph used to collapse to half its width -->
               <div id="chartWrap"><canvas id="lineChart"></canvas></div>

            </section>
            <!--'date', 'lattitude', 'longitude', 'speed', 'event']-->
            <section id='alarms' align="left">
               <div class="table-scroll">
                  <table id="alarmTable"  class="table">
                     <thead >
                        <th scope="col">date</th>
                        <th scope="col">speed</th>
                        <th scope="col">alarm</th>
                     </thead>
                     <tbody id="alarmBody">
                     </tbody>
                  </table>
               </div>
            </section>
            <section id='geofence' align="left">
               <div class="table-scroll">
                  <table id="fenceTable"  class="table">
                     <thead >
                        <tr>
                           <th scope="col">start time</th>
                           <th scope="col">end time</th>
                           <th scope="col">day of week</th>
                           <th scope="col">type</th>
                           <th scope="col">radius</th>
                           <th scope="col">audible alarm</th>
                           <th scope="col">name</th>
                           <th scope="col">action</th>
                        </tr>
                        <tr>
                           <th><input value="00:00" id="fenceStart" type="time" class="input"/></th>
                           <th><input value="00:00" id="fenceEnd" type="time" class="input_small"/></th>
                           <th>
                              <select id="fenceDay" class="input">
                                 <option value="1">Mon</option>
                                 <option value="2">Tue</option>
                                 <option value="3">Wed</option>
                                 <option value="4">Thu</option>
                                 <option value="5">Fri</option>
                                 <option value="6">Sat</option>
                                 <option value="7">Sun</option>
                                 <option value="9" selected>Every</option>
                              </select>
                           </th>
                           <th>
                              <select id="fenceType" class="input">
                                 <option value="0">In</option>
                                 <option value="1">Out</option>
                                 <option value="2">In+Out</option>
                                 <option value="3">Stay In</option>
                                 <option value="4">Exclusion zone</option>
                              </select>
                           </th>
                           <input id="fenceLat" type="hidden" step="0.00000001" style="width:7em" value=0 onchange=moveDemoFeature()></input><input id="fenceLong" type="hidden" step="0.00000001" style="width:7em" value=0 onchange=moveDemoFeature()></input>
                           <th><input id="fenceRadius" aria-label="Fence radius in metres" type="number" style="width:4em" value=100 onchange=moveDemoFeature()></input></th>
                           <th>
                              <select id="alarmEnable" class="input">
                                 <option value="0">Off</option>
                                 <option value="1" selected>On</option>
                              </select>
                           </th>
                           <th><input id="fenceName" aria-label="Fence name" style="width:5em" value=default maxlength=31 onkeydown="alphanum(this)" onkeyup="alphanum(this)" onblur="alphanum(this)" onclick="alphanum(this)"></input></th>
                           <th><button onClick=addFence() class="button">add</button></th>
                        </tr>
                     </thead>
                     <tbody id="fenceBody">
                     </tbody>
                  </table>
               </div>
            </section>
            <section id='commands' align="left">
               <div class="table-scroll">
                  <table  id="commandTable" style="width:99%" class="table">
                     <thead >
                        <tr>
                           <th>date</th>
                           <th>command</th>
                        </tr>
                        <tr>
                           <th></th>
                           <th><input type="text" name="command" id="command" aria-label="Command to send" class="input_small" /><button onclick="sendCommand(document.getElementById('command').value)" class="button" title="Send the typed command to the device">Send</button>
                              <button class="button" onclick="sendCommand('WARNAUDIO#');" title="Sound an audible alarm on the device" aria-label="Audible alarm"><i style="font-size:14pt;" class="icon bullhorn"></i></button>
                              <button class="button" onclick="sendCommand('WARNMOTOR#');" title="Vibrate the device" aria-label="Vibrate"><i  style="font-size:14pt;" class="icon exclamation-circle"></i></button>
                              <button class="button" onclick="sendCommand('PHOTO#');" title="Ask the device to take a picture" aria-label="Take a picture"><i style="font-size:14pt;" class="icon camera"></i></button>
                              <button class="button" onclick="sendCommand('RECORD#');" title="Record ten seconds from the device microphone and upload it" aria-label="Record from the device"><i style="font-size:14pt;" class="icon microphone"></i></button>
                           </th>
                        </tr>
                     </thead>
                     <tbody id="commandBody"></tbody>
                  </table>
               </div>
            </section>
            <section id='serverLogging' align="left">
               <div class="table-scroll">
                  <table  id="serverLogTable" style="width:99%" class="table">
                     <thead >
                        <th>date</th>
                        <th>line</th>
                     </thead>
                     <tbody id="serverLoggingBody"></tbody>
                  </table>
               </div>
            </section>
            <section id='trips' align="left">
            <button class="button" onclick="exitTrip();" style="font-size:10pt;"><i class="icon repeat"></i> Reload/exit current trip</button>
               <div class="table-scroll">
                  <table  id="tripsTable" style="width:99%" class="table">
                     <thead >
                        <th>start date</th>
                        <th>end date</th>
                        <th>start position</th>
                        <th>end position</th>
                        <th>distance traveled</th>
                     </thead>
                     <tbody id="tripsBody"></tbody>
                  </table>
               </div>
            </section>
            <section id='settings' align="left">
               <div class="table-scroll">
                  <table  id="settingsTable" style="width:99%" class="table">
                     <thead >
                        <th>setting</th>
                        <th>value</th>
                     </thead>
                     <tbody id="settingsBody"></tbody>
                  </table>
               </div>
            </section>
         </div>
      </div>
      <div id="aroundMap" >
         <div id="map" tabindex="0">
            <span class="mapOverlay" style="font-size:10pt;position:absolute;right:1em;bottom:0.1em;z-index: 2;" id="distance"></span>
            <span class="mapOverlay" style="font-size:10pt;position:absolute;right:1em;bottom:1.0em;z-index: 2;" id="speed"></span>
            <button type="button" class="mapOverlay mapBtn" style="font-size:22pt;position:absolute;right:1em;bottom:1.5em;z-index: 2;" onclick="toggleSat();" aria-label="Toggle satellite view"><i class="icon globe"></i></button>
            <span class="mapOverlay" style="font-size:22pt;position:absolute;right:1em;bottom:2.5em;z-index: 2;" role="img" aria-label="Battery level"><i id="batt" class="icon battery-full"></i></span>
            <span class="mapOverlay" style="font-size:22pt;position:absolute;right:1em;bottom:3.5em;z-index: 2;" role="img" aria-label="Signal strength"><i id="signal" class="icon signal"></i></span>
            <button type="button" class="mapOverlay mapBtn" style="font-size:22pt;position:absolute;right:1em;bottom:4.5em;z-index: 2;" onclick="recenter();" title="Recentre the map on the device" aria-label="Recentre on the device"><i id="current" class="icon crosshair"></i></button>

            <div id="rangeControls">
               <input value="<?php echo date('Y-m-d'); ?>" id="beginDate" aria-label="Start date" type="date" onchange="rangeEdited()" class="input"/>
               <input value="00:00" id="beginTime" type="time" onchange="rangeEdited()" class="input"/>
               <select id="hourCount" aria-label="Time range" class="input" onchange="durationEdited()">
                  <option value="720">30d</option>
                  <option value="168">7d</option>
                  <option value="72">72h</option>
                  <option value="48">48h</option>
                  <option value="24" selected>24h</option>
                  <option value="12">12h</option>
                  <option value="6" >6h</option>
                  <option value="3">3h</option>
                  <option value="1">1h</option>
               </select>
            </div>
         </div>
         <div id="popup" class="ol-popup">
            <a href="#" id="popup-closer" class="ol-popup-closer"></a>
            <div id="popup-content" style="width:20em;"></div>
         </div>
      </div>
      <script type="text/javascript" src="js/map/helper.js"></script>
      <script type="text/javascript" src="js/map/map.js"></script>
      <script type="text/javascript" src="js/map/trips.js"></script>
      <script type="text/javascript" src="js/map/mapData.js"></script>
      <script type="text/javascript" src="js/map/compatibility.js"></script>
      <script type="text/javascript" src="js/deviceData.js"></script>
   </body>
</html>
