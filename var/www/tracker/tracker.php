<?php
include 'lib.php';
include 'database.php';
validateSession();

?>
<!DOCTYPE html>
<html>
   <head>
      <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=0">
      <link rel="stylesheet" href="openlayers/ol.css" type="text/css">
      <link rel="stylesheet" href="style/tracker.css" type="text/css">
      <link rel="stylesheet" href="jquery/jquery-ui.css" type="text/css">
      <link rel="stylesheet" href="jquery/jquery.timepicker.min.css" type="text/css">
      <script src="jquery/jquery-3.2.1.min.js"></script>
      <script src="jquery/jquery-ui.js"></script>
      <script src="//cdnjs.cloudflare.com/ajax/libs/timepicker/1.3.5/jquery.timepicker.min.js"></script>
      <script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/2.8.0/Chart.bundle.min.js"></script>

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
               <a href="#account" class="tooltip"> <span class="tooltiptext"  >Account</span><i class="icon user"></i></a>
               <a href="#history" class="tooltip" ><span class="tooltiptext">History</span><i class="icon clock"></i></a>
               <a href="#stats" class="tooltip" ><span class="tooltiptext">Statistics</span><i class="icon chart-line"></i></a>
               <a href="#alarms" class="tooltip"><span class="tooltiptext">Alarms</span><i class="icon exclamation-triangle"></i></a>
               <a href="#geofence" class="tooltip"><span class="tooltiptext">Geofence</span><i class="icon map-marked-alt"></i></a>
               <a href="#commands" onClick="refreshCommandResults()" class="tooltip"><span class="tooltiptext">Commands</span><i class="icon terminal"></i></a>
               <a href="#serverLogging" class="tooltip" ><span class="tooltiptext">Server logging</span><i class="icon file"></i></a>
               <a href="#trips" class="tooltip"><span class="tooltiptext">Trips</span><i class="icon address-card"></i></a>
               <a href="#settings" class="tooltip" ><span class="tooltiptext">Settings</span><i class="icon cog"></i></a>
               <a href="#" class="tooltip"><span class="tooltiptext">Close</span><i class="icon backward"></i></a>
            </nav>
         </div>
         <div class='container' align="left">
            <section id='account' align="left">
               <button onclick="window.location.href='logout.php'" class="button">log out</button>
               <a href="#accountUpdate" class="button">change details</a>
               <div id="table-scroll">
                  <table id="deviceTable"  class="table">
                     <thead>
                        <tr>
                           <th scope="col">IMEI</th>
                           <th scope="col">Name</th>
                           <th scope="col">Actions</th>
                        </tr>
                        <tr>
                           <th><input placeholder="IMEI" id="imei" name="imei" class="input" size="17"/></th>
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
               <input placeholder="Username" id="username" name="username" class="input"/>
               <input placeholder="Name" id="name_for_account" name="name_for_account" class="input"/>
               <input placeholder="Email" id="email" name="email" class="input"/>
               <input placeholder="Password" id="password" name="password" class="input" type="password" onfocus="this.value=''"/>
               <button onclick="updateUser()" class="button">Update</button>
            </section>
            <section id='history' align="left" style="width:30vw;height:8vh;display: table;">
            <a onclick="startPlaying()" style="font-size:24pt;color:blue;display:table-cell;vertical-align:middle;text-align:center;"><i class="icon play"></i></a>
            <a onclick="pausePlaying()" style="font-size:24pt;color:blue;display:table-cell;vertical-align:middle;text-align:center;">   <i class="icon pause"></i></a>
            <a onclick="stopPlaying()" style="font-size:24pt;color:blue;display:table-cell;vertical-align:middle;text-align:center;">  <i class="icon stop"></i></a>

               <!--
                  <span class="narrow"></span>
                  <button id="downloadbtn" class="button">Download as CSV</button><span id="distance"></span>--> 
            </section>

            <section id='stats' align="left">
                <canvas id="lineChart" style="width:100%;height:90vh;"></canvas>
               <!--
                  <span class="narrow"></span>
                  <button id="downloadbtn" class="button">Download as CSV</button><span id="distance"></span>--> 
            </section>
            <!--'date', 'lattitude', 'longitude', 'speed', 'event']-->
            <section id='alarms' align="left">
               <div id="table-scroll">
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
               <div id="table-scroll">
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
                                 <option value="8" selected>Every</option>
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
                           <th><input id="fenceRadius" type="number" style="width:4em" value=100 onchange=moveDemoFeature()></input></th>
                           <th>
                              <select id="alarmEnable" class="input">
                                 <option value="0">Off</option>
                                 <option value="1" selected>On</option>
                              </select>
                           </th>
                           <th><input id="fenceName" style="width:5em" value=default maxlength=31 onkeydown="alphanum(this)" onkeyup="alphanum(this)" onblur="alphanum(this)" onclick="alphanum(this)"></input></th>
                           <th><button onClick=addFence() class="button">add</button></th>
                        </tr>
                     </thead>
                     <tbody id="fenceBody">
                     </tbody>
                  </table>
               </div>
            </section>
            <section id='commands' align="left">
               <div id="table-scroll">
                  <table  id="commandTable" style="width:99%" class="table">
                     <thead >
                        <tr>
                           <th>date</th>
                           <th>command</th>
                        </tr>
                        <tr>
                           <th></th>
                           <th><input type="text" name="command" id="command" class="input_small" /><button onclick="sendCommand(document.getElementById('command').value)" class="button">Send</button>
                              <button class="button" onclick="sendCommand('WARNAUDIO#');"><i style="font-size:14pt;" class="icon bullhorn"></i></button>
                              <button class="button" onclick="sendCommand('WARNMOTOR#');"><i  style="font-size:14pt;" class="icon exclamation-circle"></i></button>
                           </th>
                        </tr>
                     </thead>
                     <tbody id="commandBody"></tbody>
                  </table>
               </div>
            </section>
            <section id='serverLogging' align="left">
               <div id="table-scroll">
                  <table  id="commandTable" style="width:99%" class="table">
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
               <div id="table-scroll">
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
               <div id="table-scroll">
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
            <a style="font-size:10pt;color:blue;position:absolute;right:1em;bottom:0.1em;z-index: 2;" id="distance"></a>
            <a style="font-size:10pt;color:blue;position:absolute;right:1em;bottom:1.0em;z-index: 2;" id="speed"></a>
            <a style="font-size:22pt;color:red;position:absolute;right:1em;bottom:1.5em;z-index: 2;" onclick="toggleSat();"><i class="icon globe"></i></a>
            <a style="font-size:22pt;color:blue;position:absolute;right:1em;bottom:2.5em;z-index: 2;"><i id="batt" class="icon battery-full"></i></a>
            <a style="font-size:22pt;color:blue;position:absolute;right:1em;bottom:3.5em;z-index: 2;"><i id="signal" class="icon signal"></i></a>
            <a style="font-size:22pt;color:blue;position:absolute;right:1em;bottom:4.5em;z-index: 2;" onclick="recenter();"><i id="current" class="icon crosshair"></i></a>

            <div  style="color:red;position:absolute;right:3pt;top:3pt;z-index: 2;">
               <input value="<?php echo date('Y-m-d'); ?>" id="beginDate" type="date" onchange="searchdateChange()" class="input"/>
               <input value="00:00" id="beginTime" type="time" onchange="searchdateChange()" class="input"/>
               <select id="hourCount" class="input" onchange="searchdateChange()">
                  <option value="72">72 Hours</option>
                  <option value="48">48 Hours</option>
                  <option value="24">24 Hours</option>
                  <option value="12">12 Hours</option>
                  <option value="6" selected>6 Hours</option>
                  <option value="3">3 Hours</option>
                  <option value="1">1 Hour</option>
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
