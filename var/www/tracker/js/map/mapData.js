var layer = 0;
var tripActive = false;

function changeLayer() {
    if (layer == 0) {
        satLayer.setVisible(false);
        arcgisLayer.setVisible(true);
        osmLayer.setVisible(true);
    } else {
        satLayer.setVisible(true);
        arcgisLayer.setVisible(false);
        osmLayer.setVisible(false);
    }
}

function toggleSat() {
    if (layer == 1) {
        layer = 0;
    } else {
        layer = 1;
    }
    changeLayer();
}

var newImei = new Date().getTime();
var animateSpeed=200;

function animateTo(long, lat) {
    setfocus = 0;
    var lonlat = ol.proj.fromLonLat([long, lat]);

    map.getView().animate({
        center: lonlat,
        duration: animateSpeed
    });
}

function constructURL(phpName, begindate, enddate) {
    return phpName + "?imei=" + imei + "&begin=" + tounix(begindate) + (enddate ? ("&end=" + tounix(enddate)) : "") + viewOnlyParameter();
}

//the daemon leaves the speed field empty for cell tower fixes rather than writing a
//zero, because a tower cannot measure speed and a zero would claim the device had
//stopped. parseFloat gives NaN for the empty field, so render it as unknown.
function speedText(spd) {
    var v = parseFloat(spd);
    return isFinite(v) ? v + ' km/h' : 'not measured';
}

/* The device server writes a picture upload to the event log as "photo:<unix ts>", which is
   the same timestamp that heads the record inside <imei>.images.db. That is all image.php
   needs to find it, so nothing here has to know where the file lives or how it is laid out. */
function photoTsOf(text) {
    var m = /^photo:(\d{1,20})$/.exec(String(text || '').trim());
    return m ? m[1] : null;
}

/* A monitor recording is logged the same way a picture is, as "audio:<unix ts>". */
function audioTsOf(text) {
    var m = /audio:(\d{1,20})/.exec(String(text || ''));
    return m ? m[1] : null;
}

function audioUrl(ts) {
    return 'image.php?kind=audio&imei=' + encodeURIComponent(imei) + '&ts=' + encodeURIComponent(ts);
}

/* The watch records AMR, which no browser decodes, so the player is pointed at the
   converted stream while the link beside it still hands over the original bytes. */
function audioPlayUrl(ts) {
    return audioUrl(ts) + '&play=1';
}

function audioMarkup(ts) {
    return "<span class='audioBlock' onclick='event.stopPropagation();'>"
           + "<audio class='audioPlayer' controls preload='none' src='"
           + escapeHtml(audioPlayUrl(ts)) + "'></audio>"
           + "<a class='audioLink' href='" + escapeHtml(audioUrl(ts)) + "'"
           + " download='recording-" + escapeNumber(ts) + ".amr'>original .amr</a></span>";
}

function photoUrl(ts) {
    return 'image.php?imei=' + encodeURIComponent(imei) + '&ts=' + encodeURIComponent(ts);
}

/* Opens the picture full size. The viewer is built once and reused; clicking anywhere on it
   or pressing escape puts it away again. */
function showPhoto(ts) {
    var box = document.getElementById('photoViewer');

    if (!box) {
        box = document.createElement('div');
        box.id = 'photoViewer';
        box.className = 'photoViewer';
        box.innerHTML = '<img alt="Picture from the device">';
        box.addEventListener('click', hidePhoto);
        document.body.appendChild(box);
        document.addEventListener('keydown', function (e) {
            if (e.key === 'Escape') {
                hidePhoto();
            }
        });
    }

    box.querySelector('img').src = photoUrl(ts);
    box.style.display = 'flex';
}

function hidePhoto() {
    var box = document.getElementById('photoViewer');

    if (box) {
        box.style.display = 'none';
        //drop the bytes rather than hold every picture that has been looked at
        box.querySelector('img').removeAttribute('src');
    }
}

/* Each poll asks for rows from the newest one already held, and the server includes that
   row again rather than starting after it, so a straight concat re-adds the boundary row
   every few seconds. It was invisible while rows were plain text and obvious as soon as
   they carried a picture. Rows are identified by their time and their text, which is what
   distinguishes them on screen too. */
function concatNewRows(existing, parsed) {
    var seen = {};

    for (var i = 0; i < existing.length; i++) {
        seen[(+existing[i][0]) + '|' + existing[i].slice(1).join(',')] = true;
    }

    var fresh = parsed.filter(function (row) {
        var key = (+row[0]) + '|' + row.slice(1).join(',');

        if (seen[key]) {
            return false;
        }

        seen[key] = true;
        return true;
    });

    return existing.concat(fresh);
}

function computeEventRow(cols) {
    var ts = photoTsOf(cols[4]);
    var ats = audioTsOf(cols[4]);
    //a picture says more than the word "photo" does, so the row carries the thumbnail
    //itself. Clicking the image opens it full size; clicking the rest of the row still
    //moves the map, which is what every other event row does.
    var last = ts
        ? "<td><img class='photoThumb' src='" + escapeHtml(photoUrl(ts)) + "' alt='Picture from the device' loading='lazy' onclick='event.stopPropagation();showPhoto(" + escapeNumber(ts) + ")'></td>"
        : ats ? "<td>" + audioMarkup(ats) + "</td>"
        : "<td>" + escapeHtml(cols[4]) + "</td>";
    return "<tr onclick='animateTo(" + escapeNumber(cols[2]) + "," + escapeNumber(cols[1]) + ")'><td>" + escapeHtml(readableDate(new Date(cols[0]))) + "</td><td>" + escapeHtml(speedText(cols[3])) + "</td>" + last + "</tr>";
}

function computeHistoryRow(cols) {
    //escaped like every other row builder here. This one was missed: the two coordinates went
    //straight into an onclick attribute and into the cells, while computeEventRow directly
    //above does the same job through escapeNumber and escapeHtml. They hold whatever is in the
    //position file, which this server writes as numbers - but so does the row above, and that
    //one does not rely on it.
    return "<tr onclick='animateTo(" + escapeNumber(cols[2]) + "," + escapeNumber(cols[1]) + ")'><td>" + escapeHtml(readableDate(new Date(cols[0]))) + "</td><td>" + escapeHtml(cols[1]) + "</td><td>" + escapeHtml(cols[2]) + "</td><td>" + escapeHtml(speedText(cols[3])) + "</td></tr>";
}

function computeLogRow(cols) {
    //A command result that reports a picture gets the picture itself, so the answer to
    //"take a photo" is visible where the command was sent rather than only on the map.
    //The same "photo:<ts>" marker the event log uses, so there is one rule for both.
    var text = String(cols[1] || '');
    var am = /audio:(\d{1,20})/.exec(text);

    if (am) {
        return "<tr><td>" + escapeHtml(readableDate(new Date(cols[0]))) + "</td><td style='font-size:10px'>"
               + escapeHtml(text) + "<br>" + audioMarkup(am[1]) + "</td></tr>";
    }

    var m = /photo:(\d{1,20})/.exec(text);
    var extra = '';

    if (m) {
        extra = "<br><img class='photoThumb' src='" + escapeHtml(photoUrl(m[1]))
                + "' alt='Picture from the device' loading='lazy'"
                + " onclick='showPhoto(" + escapeNumber(m[1]) + ")'>";
    }

    return "<tr><td>" + escapeHtml(readableDate(new Date(cols[0]))) + "</td><td style='font-size:10px'>" + escapeHtml(text) + extra + "</td></tr>";
}

function computeFenceRow(cols) {
    var dayOfWeek = ['', 'Mon', 'Tues', 'Wed', 'Thurs', 'Fri', 'Sat', 'Sun', '', 'Every'];
    var fenceType = ['In', 'Out', 'In+Out', 'Stay in', 'Exclusion zone'];
    var alarmEnabled = ['Off', 'On'];
    var dateMod=localTime(cols[0])[0] ;
    var displayDate=parseInt(cols[2]) + dateMod;

    if(displayDate==0 ){
          displayDate=7;   
    }

    if(displayDate==8){
        displayDate=0;
    }


    return "<tr onclick='animateTo(" + escapeNumber(cols[5]) + "," + escapeNumber(cols[4]) + ")'><td>" + escapeHtml(localTime(cols[0])[1]) + "</td><td>" + escapeHtml(localTime(cols[1])[1]) + "</td><td>" + escapeHtml(dayOfWeek[displayDate]) + "</td><td>" + escapeHtml(fenceType[cols[3]]) + "</td><td>" + escapeNumber(cols[6]) + "m</td><td>" + escapeHtml(alarmEnabled[cols[7]]) + "</td><td>" + escapeHtml(cols[8]) + "</td><td><button class='button' onClick='deleteFence(\"" + escapeHtml(cols.join(',')) + "\")' >delete</button></td></tr>";
}


function tableHeader(cols) {
    const tableHead = document.getElementById("dataHead");

    tableHead.innerHTML = '';

    cols.forEach(col => {
        th = document.createElement('th');
        th.innerHTML = col;
        tableHead.appendChild(th);
    });
}

function enableDownload(data, enabled) {
    /*   const downloadbtn = document.getElementById("downloadbtn");

       if (enabled == false) {
           downloadbtn.enabled = false;
           return;
       }

       downloadbtn.enabled = true;
       downloadbtn.onclick = function() {
           var hiddenElement = document.createElement('a');
           hiddenElement.href = 'data:text/csv;charset=utf-8,' + encodeURI(data);
           hiddenElement.target = '_blank';

           //provide the name for the CSV file to be downloaded
           hiddenElement.download = 'dataset.csv';
           hiddenElement.click();
       }*/
}


// request permission on page load
function requestEventPermissions() {
    if (!Notification) {
        return;
    }

    if (Notification.permission !== 'granted') {
        Notification.requestPermission();
    }
}

function eventNotification(msg) {
    if (!!Notification && Notification.permission == 'granted')
        var notification = new Notification('Notification title', {
            icon: 'pin.png',
            body: msg,
        });
}
requestEventPermissions();


function getSelectedBeginDate() {
    return combineDT('beginDate', 'beginTime');

}

function arrayBeginDate(arr) {
    return arr.length == 0 ? getSelectedBeginDate() : new Date(arr[arr.length - 1][0].valueOf() + 3);
}


function getSelectedEndDate() {
    const endDate = new Date();
    endDate.setTime(getSelectedBeginDate().getTime() + (document.getElementById('hourCount').value * 60 * 60 * 1000));
    return endDate;
}

var lastEvent = null;
var eventList = [];

//for trip fetch events from start to end of trip
function fetchEvents() {
    $.ajax({
        url: constructURL("events.php", arrayBeginDate(eventList), getSelectedEndDate()),
        success: function(result) {
            enableDownload(result, true);
            var parsed = forEachRow(result, 4, cols => [new Date(cols[0]), parseFloat(cols[1]), parseFloat(cols[2]), parseFloat(cols[3]), cols[4]]);

            if (parsed.length == 0) {
                return;
            }

            eventList = concatNewRows(eventList, parsed);

            var lastCaption;
            var features = [];
            eventList.forEach(rv => {
                //The caption is dropped into innerHTML further down and rv[4] is whatever
                //the device sent, so it is escaped here rather than trusted. A picture event
                //additionally gets the image itself, built only from digits that matched the
                //photo pattern.
                var pts = photoTsOf(rv[4]);
                var caption = escapeHtml(rv[4]) + ' on ' + escapeHtml(readableDate(rv[0]))
                              + ' while moving at speed: ' + escapeHtml(speedText(rv[3]));

                if (pts) {
                    caption = 'Picture taken ' + escapeHtml(readableDate(rv[0]))
                              + "<br><img class='photoPopup' src='" + escapeHtml(photoUrl(pts))
                              + "' alt='Picture from the device' onclick='showPhoto(" + escapeNumber(pts) + ")'>";
                }

                var apts = audioTsOf(rv[4]);

                if (apts) {
                    caption = 'Recording made ' + escapeHtml(readableDate(rv[0])) + '<br>' + audioMarkup(apts);
                }

                lastCaption = caption;

                //an event logged before the device had a fix carries 0,0, and a few older
                //ones carry values well outside the valid range. drawing those puts a
                //marker on Null Island off west africa, which reads as the middle of the
                //atlantic. the event itself is real - an SOS or a low battery still
                //happened - so it keeps its row in the table below and still raises a
                //notification. it just gets no marker, because we do not know where it was.
                var lat = rv[1], lng = rv[2];

                if (!isFinite(lat) || !isFinite(lng) || Math.abs(lat) > 90 || Math.abs(lng) > 180 || (lat === 0 && lng === 0)) {
                    return;
                }

                var f = new ol.Feature(new ol.geom.Circle(ol.proj.fromLonLat([lng, lat]), 5));
                f.EVT = rv[4];
                f.CAPTION = caption;
                features.push(f);
            });
            eventLayer.getSource().clear();
            eventLayer.getSource().addFeatures(features);

            if (parsed.length && lastCaption && !tripActive) {
                eventNotification(lastCaption);
            }

            const tableBody = document.getElementById("alarmBody");
            tableBody.innerHTML = eventList.slice().reverse().map(rv => computeEventRow(rv)).join('');
        }
    });
}


function filterEvents(beginDate, endDate) {
    var filtered = eventList.filter(cols => cols[0] >= beginDate && cols[0] <= endDate);

    const tableBody = document.getElementById("alarmBody");
    tableBody.innerHTML = filtered.reverse().map(rv => computeEventRow(rv)).join('');
}


function totalDistance(elems) {
    var totalDistance = 0;

    for (var i = 0; i < elems.length - 1; i++) {
        totalDistance += haversineDistance(elems[i][1], elems[i][2], elems[i + 1][1], elems[i + 1][2])
    }
    return totalDistance;
}


var historyItems = [];

function refreshHistory() {
    var coords = historyItems.map(rv => (ol.proj.fromLonLat([rv[2], rv[1]])));

    if (!tripActive) {
        travelFeature.getGeometry().setCoordinates(coords);
        travelFeature.setStyle(travelLayerStyle(travelFeature));
    }

    var dist = totalDistance(historyItems);

    var distanceDiv = document.getElementById("distance");
    distanceDiv.innerHTML = '  distance: ' + (Math.round(dist * 100) / 100) + 'km';
}

function fetchHistory() {
    $.ajax({
        url: constructURL("history.php", arrayBeginDate(historyItems), getSelectedEndDate()),
        success: function(result) {
            enableDownload(result, true);
            var parsed = forEachRow(result, 4, cols => [new Date(cols[0]), parseFloat(cols[1]), parseFloat(cols[2]), parseFloat(cols[3]), parseInt(cols[4])]);
            parsed = parsed.filter(cols => cols[4] != 1);

            if (parsed.length == 0) {
                return;
            }
            historyItems = historyItems.concat(parsed);
            refreshHistory();
            refreshTrips();
        }
    });
}



var serverLogging = [];

function fetchLogging() {
    $.ajax({
        url: constructURL("logging.php", arrayBeginDate(serverLogging), getSelectedEndDate()),
        success: function(result) {
            enableDownload(result, true);

            var parsed = forEachRow(result, 1, cols => [new Date(cols[0]), cols.slice(1).join(',')]);

            if (parsed.length == 0) {
                return;
            }

            serverLogging = serverLogging.concat(parsed);

            const tableBody = document.getElementById("serverLoggingBody");
            tableBody.innerHTML = serverLogging.slice().reverse().map(rv => computeLogRow(rv)).join('');

        }
    });
}

function filterLogging(beginDate, endDate) {
    var filtered = serverLogging.filter(cols => cols[0] >= beginDate && cols[0] <= endDate);

    const tableBody = document.getElementById("serverLoggingBody");
    tableBody.innerHTML = filtered.reverse().map(rv => computeLogRow(rv)).join('');
}


var commandResults = [];

function fetchCommandResults() {
    $.ajax({
        url: constructURL("command_output.php", arrayBeginDate(commandResults), getSelectedEndDate()),
        success: function(result) {
            enableDownload(result, true);

            var parsed = forEachRow(result, 1, cols => [new Date(cols[0]), cols.slice(1).join(',')]);

            if (parsed.length == 0) {
                return;
            }

            commandResults = concatNewRows(commandResults, parsed);

            const tableBody = document.getElementById("commandBody");
            tableBody.innerHTML = commandResults.slice().reverse().map(rv => computeLogRow(rv)).join('');
        }
    });
}

function filterCommandResults(beginDate, endDate) {
    var filtered = commandResults.filter(cols => cols[0] >= beginDate && cols[0] <= endDate);

    const tableBody = document.getElementById("commandBody");
    tableBody.innerHTML = filtered.reverse().map(rv => computeLogRow(rv)).join('');
}


function refreshTrips() {
    const tableBody = document.getElementById("tripsBody");
    tableBody.innerHTML = '';
    convert_to_trips(tableBody, historyItems);
}

function sendCommand(cmd) {
    $.ajax({
        url: "command.php?imei=" + imei + "&command=" + encodeURIComponent(cmd),
        success: function(result) {
            for (let i = 2000; i < 20000; i += 2000) setTimeout(function() {
                fetchCommandResults();
            }, i);
        }
    });
}

function addFence() {
    var startTime=utcTime(document.getElementById("fenceStart").value);
    var endTime=utcTime(document.getElementById("fenceEnd").value)[1];
    var fenceDay=parseInt(document.getElementById("fenceDay").value)-startTime[0];
    if(fenceDay==0)fenceDay=7;
    if(fenceDay==8)fenceDay=1;

    var f = [startTime[1], endTime, parseInt(document.getElementById("fenceDay").value)-startTime[0], document.getElementById("fenceType").value,
        document.getElementById("fenceLat").value, document.getElementById("fenceLong").value, document.getElementById("fenceRadius").value,
        document.getElementById("alarmEnable").value, document.getElementById("fenceName").value
    ];

    var cols = f.join(',');

    //this used to also build a url with the username and password from the account form
    //appended to it and copy that to the clipboard on every fence added - putting the
    //user's password into their clipboard, and into anywhere they subsequently pasted it.
    //it was never needed: the request below creates the fence using the session.

    $.ajax({
        url: "geofence.php?imei=" + imei + "&action=write&fence=" + encodeURIComponent(cols),
        success: function(result) {
            fetchFence();
        }
    });
}

function deleteFence(cols) {
    $.ajax({
        url: "geofence.php?imei=" + imei + "&action=remove&fence=" + encodeURIComponent(cols),
        success: function(result) {
            fetchFence();
        }
    });
}


function clearFence() {
    $.ajax({
        url: "geofence.php?imei=" + imei + "&action=clear",
        success: function(result) {
            fetchFence();
        }
    });
}

function alphanum(o) {
    o.value = o.value.replace(/([^0-9A-Za-z -_])/g, "");
}

/*
 * A circle drawn on a web mercator map is in projected units, not metres. They are only the
 * same at the equator: at latitude p a real metre is 1/cos(p) projected ones. The radius stored
 * for a fence is in metres - the daemon divides it by 1000 and compares it against a haversine
 * distance in kilometres - so it has to be scaled before it is drawn, or the circle on screen
 * is not the fence the daemon enforces. It used to be multiplied by 1.60934, which is close to
 * 1/cos(52) and so came out about right in the Netherlands and nowhere else.
 */
function fenceCircle(lat, lng, metres) {
    return new ol.geom.Circle(ol.proj.fromLonLat([lng, lat]), metres / Math.cos(lat * Math.PI / 180));
}

//the preview is a feature like any other on the fence layer, so a refresh of the layer takes it
//with it. find it, or put it back.
function demoFeature() {
    var found = null;

    geofenceLayer.getSource().forEachFeature(function(feature) {
        if (feature.TYPE == 'demo') {
            found = feature;
        }
    });

    if (!found) {
        found = new ol.Feature();
        found.TYPE = 'demo';
        geofenceLayer.getSource().addFeature(found);
    }

    return found;
}

function moveDemoFeature() {
    var lng = parseFloat(document.getElementById("fenceLong").value);
    var lat = parseFloat(document.getElementById("fenceLat").value);
    var radius = parseFloat(document.getElementById("fenceRadius").value);
    var feature = demoFeature();

    //the lat/long inputs are hidden and start at 0, so until a point has been picked there is
    //no position to preview. clear the geometry rather than draw the preview in the gulf of
    //guinea.
    if (!isFinite(lat) || !isFinite(lng) || !isFinite(radius) || (lat === 0 && lng === 0)) {
        feature.setGeometry(null);
        return;
    }

    feature.setGeometry(fenceCircle(lat, lng, radius));
}

function fetchFence() {
    $.ajax({
        url: "geofence.php?imei=" + imei + viewOnlyParameter(),
        success: function(result) {
            enableDownload(result, false);
            var parsed = forEachRow(result, 8, cols => [cols[0], cols[1], cols[2], cols[3], parseFloat(cols[4]), parseFloat(cols[5]), parseFloat(cols[6]), cols[7], cols[8]]);


            var coords = parsed.map(rv => {
                var f = new ol.Feature(fenceCircle(rv[4], rv[5], rv[6]));
                f.TYPE = rv[3];
                return f;
            });

            geofenceLayer.getSource().clear();
            geofenceLayer.getSource().addFeatures(coords);

            //The preview lives on this layer, so clearing it takes the preview with it - and
            //this runs on the refresh timer, which used to wipe a point the user had just
            //picked while they were still setting the radius. Put it back and redraw it from
            //whatever the panel currently says.
            moveDemoFeature();

            const tableBody = document.getElementById("fenceBody");
            tableBody.innerHTML = '';
            tableBody.innerHTML = parsed.map(rv => computeFenceRow(rv)).join('');
        }
    });
}



function refreshSettings() {
    $.ajax({
        url: "disabled_alarms.php?imei=" + imei + "&action=read" + viewOnlyParameter(),
        success: function(result) {
            enableDownload('', false);
            const tableBody = document.getElementById("settingsBody");
            tableBody.innerHTML = '';
            tableBody.innerHTML += '<tr><td>disabled alarms</td><td><input id="disabledAlarms" class="input_small" value =\"' + result + '\"/><button onclick="saveSettings()"  class="button">Save</button></td></tr>';
        }
    });
}


function saveSettings() {
    $.ajax({
        url: "disabled_alarms.php?imei=" + imei + "&action=write&alarms=" + document.getElementById("disabledAlarms").value,
        success: function(result) {
            alert('settings saved.');
            refreshSettings();
        }
    });
}

var lineChart = null;
var chartSignature = null;

var stepIndex = 0;
var isPlaying = 0;

var statsList = [];

//The chart sits on a dark panel, so Chart.js' default mid grey for ticks and labels came
//out barely readable. Everything the chart draws in a neutral colour is set from here.
var CHART_INK = '#c9cfdb';
var CHART_GRID = 'rgba(255, 255, 255, 0.08)';

//A stat is drawn on the left axis or the right one depending on the range it lives in.
//Every series used to share a single axis, so speed (0-10) and the satellite counts were
//pressed into the bottom few percent of a scale that blood pressure stretched to 140, and
//on a phone they were a flat line along the bottom. The groups drive the filter chips.
var STAT_SERIES = {
    heartrate:     { group: 'vitals',   axis: 'y',  colour: '#ff5c7a', label: 'heart rate',    unit: 'bpm' },
    systole:       { group: 'vitals',   axis: 'y',  colour: '#ffa14a', label: 'systolic',      unit: 'mmHg' },
    diastole:      { group: 'vitals',   axis: 'y',  colour: '#ffd166', label: 'diastolic',     unit: 'mmHg' },
    SPO2:          { group: 'vitals',   axis: 'y',  colour: '#4cc9f0', label: 'blood oxygen',  unit: '%' },
    temperature:   { group: 'vitals',   axis: 'y',  colour: '#f78fb3', label: 'temperature',   unit: '\u00b0C' },
    speed:         { group: 'activity', axis: 'y2', colour: '#06d6a0', label: 'speed',         unit: 'km/h' },
    steps_k:       { group: 'activity', axis: 'y2', colour: '#a7e34d', label: 'steps',         unit: 'thousand' },
    sleep_deep:    { group: 'sleep',    axis: 'y',  colour: '#7c5cff', label: 'sleep',         unit: 'min' },
    sleep_light:   { group: 'sleep',    axis: 'y',  colour: '#b3a0ff', label: 'light sleep',   unit: 'min' },
    sleep_score:   { group: 'sleep',    axis: 'y2', colour: '#5f7cff', label: 'sleep recorded', unit: '' },
    sleep_tst:     { group: 'sleep',    axis: 'y',  colour: '#8f7bff', label: 'total sleep',   unit: 'min' },
    sleep_spt:     { group: 'sleep',    axis: 'y',  colour: '#6f5ce0', label: 'sleep period',  unit: 'min' },
    sleep_waso:    { group: 'sleep',    axis: 'y',  colour: '#ff8fb0', label: 'awake in bed',  unit: 'min' },
    sleep_efficiency: { group: 'sleep', axis: 'y2', colour: '#4de3c1', label: 'efficiency',    unit: '%' },
    sleep_wakeups: { group: 'sleep',    axis: 'y2', colour: '#ffc75f', label: 'awakenings',    unit: '' },
    sleeping:      { group: 'sleep',    axis: 'y2', colour: '#a06bff', label: 'asleep',        unit: '' },
    sleep_day:     { group: 'sleep',    axis: 'y',  colour: '#c0a0ff', label: 'slept today',   unit: 'min' },
    battery_level: { group: 'device',   axis: 'y',  colour: '#9d8df1', label: 'battery',       unit: '%' },
    signal:        { group: 'device',   axis: 'y',  colour: '#6c8cff', label: 'gsm signal',    unit: '' },
    gps_sats:      { group: 'device',   axis: 'y2', colour: '#c8b6ff', label: 'gps satellites', unit: '' },
    wifi_networks: { group: 'device',   axis: 'y2', colour: '#7bdff2', label: 'wifi networks', unit: '' },
    lbs_stations:  { group: 'device',   axis: 'y2', colour: '#b0b6c8', label: 'cell towers',   unit: '' }
};

var STAT_GROUPS = [
    { key: 'vitals',   label: 'Vitals' },
    { key: 'activity', label: 'Activity' },
    { key: 'sleep',    label: 'Sleep' },
    { key: 'device',   label: 'Device' },
    { key: 'other',    label: 'Other' }
];

//the device health series are the noisiest and the least often wanted, so they start folded
//away rather than crowding eleven other lines off a phone screen. one tap brings them back
//and the choice is remembered.
//store.js is not loaded on this page, so this keeps its own storage rather than reaching for
//helpers that are not there. private browsing can refuse localStorage outright, in which case
//the choice simply does not persist.
var HIDDEN_GROUPS_KEY = 'statGroupsHidden';

function hiddenStatGroups() {
    try {
        var stored = window.localStorage.getItem(HIDDEN_GROUPS_KEY);

        if (stored === null) {
            return ['device'];
        }

        return stored ? stored.split('|') : [];

    } catch (e) {
        return ['device'];
    }
}

function toggleStatGroup(key) {
    var hidden = hiddenStatGroups();
    var at = hidden.indexOf(key);

    if (at === -1) {
        hidden.push(key);
    } else {
        hidden.splice(at, 1);
    }

    try {
        window.localStorage.setItem(HIDDEN_GROUPS_KEY, hidden.join('|'));
    } catch (e) {
        //not being able to remember the choice is not a reason to ignore it
    }

    renderStatChips();
    makeChart(makeDataset(statsShown));
}

function statMeta(name) {
    if (STAT_SERIES[name]) {
        return STAT_SERIES[name];
    }

    //an unrecognised stat still gets a readable colour rather than the old hash, which
    //multiplied character codes into steps of 25 and could land on near black, or on a
    //shade another series already had
    var hash = 0;

    for (var i = 0; i < name.length; i++) {
        hash = (hash * 31 + name.charCodeAt(i)) % 360;
    }

    return { group: 'other', axis: 'y', colour: 'hsl(' + hash + ', 70%, 62%)', label: name, unit: '' };
}

function narrowScreen() {
    return window.innerWidth < 700;
}

//Thin a series down to roughly what the canvas can actually resolve. Every bucket keeps its
//lowest and its highest reading, so a heart rate peak survives the thinning instead of being
//skipped over - a plain stride would drop exactly the moments worth seeing.
function decimate(points, budget) {
    if (points.length <= budget) {
        return points;
    }

    var perBucket = points.length / budget;
    var out = [];

    for (var b = 0; b < budget; b++) {
        var from = Math.floor(b * perBucket);
        var to = Math.min(points.length, Math.floor((b + 1) * perBucket));

        if (to <= from) {
            continue;
        }

        var lo = from;
        var hi = from;

        for (var i = from; i < to; i++) {
            if (points[i].y < points[lo].y) { lo = i; }
            if (points[i].y > points[hi].y) { hi = i; }
        }

        if (lo === hi) {
            out.push(points[lo]);
        } else if (lo < hi) {
            out.push(points[lo], points[hi]);
        } else {
            out.push(points[hi], points[lo]);
        }
    }

    return out;
}

//A straight line drawn across a gap in the data claims readings that were never taken - the
//device goes quiet for hours at a time. Break the line instead.
var STAT_GAP_MS = 45 * 60 * 1000;

// Build the plotted points for one series: split the real readings into runs separated by a
// genuine gap, thin each run on its own, then join the runs with a break. The gap is judged from
// the series' own cadence, so it is the same on a 24 hour view and a 30 day view.
//
// This is the fix for lines that came out broken into dots. The break used to be applied after
// the thinning, and thinning a month down to a thousand points leaves them tens of minutes
// apart - past the fixed 45 minute gap - so every segment was cut and, with no point markers,
// the line vanished. Segmenting the raw readings first keeps a continuous line continuous
// however wide the range is.
function seriesData(points, budget) {
    if (points.length === 0) {
        return [];
    }

    var deltas = [];

    for (var i = 1; i < points.length; i++) {
        deltas.push(points[i].x - points[i - 1].x);
    }

    var sorted = deltas.slice().sort(function(a, b) { return a - b; });
    var median = sorted.length ? sorted[Math.floor(sorted.length / 2)] : 0;

    // a run is broken only by a gap several times the normal spacing, never below the 45 minute
    // floor - so an ordinary sampling interval never breaks the line, and a sparse series that is
    // simply reported hourly does not come out as loose dots either
    var threshold = Math.max(median * 3, STAT_GAP_MS);

    var segments = [];
    var run = [points[0]];

    for (var i = 1; i < points.length; i++) {
        if (points[i].x - points[i - 1].x > threshold) {
            segments.push(run);
            run = [];
        }

        run.push(points[i]);
    }

    segments.push(run);

    // thin each run to a share of the budget in proportion to its length, and separate the runs
    // with a single null so the line breaks across a real gap but stays whole within a run
    var out = [];

    for (var s = 0; s < segments.length; s++) {
        var seg = segments[s];
        var share = Math.max(2, Math.round(budget * seg.length / points.length));

        if (s > 0) {
            out.push({ x: new Date(seg[0].x.getTime() - 1), y: null });
        }

        var thinned = decimate(seg, share);

        for (var k = 0; k < thinned.length; k++) {
            out.push(thinned[k]);
        }
    }

    return out;
}

var statsShown = [];

//which groups this device actually reports. a chip for a group nothing falls into is a filter
//for nothing, and four of them wrapped onto a second row on a phone.
var statGroupsPresent = [];

function makeDataset(itemList) {
    statsShown = itemList;

    var hidden = hiddenStatGroups();
    var budget = narrowScreen() ? 400 : 1000;
    var byType = {};

    //one pass, rather than filtering the whole list once per series
    for (var i = 0; i < itemList.length; i++) {
        var name = itemList[i][1];

        //a torn log line used to fuse a stat name onto the timestamp of the next row and
        //left entries like "temperature2025-09-04T03:45:10Z" in the legend. anything that
        //is not a plain name is not a series.
        if (!/^[A-Za-z][A-Za-z0-9_]*$/.test(name)) {
            continue;
        }

        if (!byType[name]) {
            byType[name] = [];
        }

        //a non-numeric value would draw as a break in the line; it is not a reading, so drop it
        if (!isFinite(itemList[i][2])) {
            continue;
        }

        byType[name].push({ x: itemList[i][0], y: itemList[i][2] });
    }

    var present = {};

    Object.keys(byType).forEach(function(name) {
        present[statMeta(name).group] = true;
    });

    statGroupsPresent = Object.keys(present);

    var datasets = [];

    Object.keys(byType).sort().forEach(function(name) {
        var meta = statMeta(name);

        if (hidden.indexOf(meta.group) !== -1) {
            return;
        }

        var points = byType[name].sort(function(a, b) { return a.x - b.x; });

        datasets.push({
            //the legend is where someone finds out that the orange line is systolic pressure
            //and the green one is km/h, so the unit belongs in the name
            label: meta.unit ? meta.label + ' (' + meta.unit + ')' : meta.label,
            unit: meta.unit,
            yAxisID: meta.axis,
            backgroundColor: meta.colour,
            borderColor: meta.colour,
            borderWidth: 2,
            pointRadius: 0,
            pointHoverRadius: 4,
            pointHitRadius: 14,
            tension: 0,
            fill: false,
            spanGaps: false,
            data: seriesData(points, budget)
        });
    });

    return datasets;
}

//The range pickers float over the top right corner and sit above this panel, because they
//choose what the graph draws. Their height is not fixed - on a narrow screen they wrap onto a
//second line - so the space held clear for them is measured rather than guessed. Without this
//the chip row ends up underneath them.
function layoutStatsPanel() {
    var panel = document.getElementById('stats');

    if (!panel) {
        return;
    }

    var controls = document.getElementById('rangeControls');
    var root = document.documentElement;

    //Published as a custom property rather than written onto one panel, because every panel
    //holds the same room clear for the pickers. It is measured because it is not a fixed
    //height - the pickers are one row or two depending on how much width there is.
    //The rail no longer needs measuring: the panels are positioned inside .container, which
    //begins where the rail ends.
    if (controls && root.style.setProperty) {
        root.style.setProperty('--pickers', controls.offsetHeight + 'px');
    }
}

//Which panel is open is published as a class on the body, because two things outside the panel
//depend on it: the dark ground drawn behind it over the whole window, and the map's own
//readouts in the bottom right, which the stylesheet takes out of the layout while the graph is
//up rather than leaving them over the plot.
function syncStatsMode() {
    var hash = window.location.hash.replace('#', '');
    var target = hash ? document.getElementById(hash) : null;

    //the close link is href="#", which is a hash that names nothing
    var isPanel = !!(target && target.tagName === 'SECTION');

    //Some panels sit over the map rather than instead of it, and get no dark ground behind
    //them: playback is transport controls for a trip drawing itself on the map, a geofence is
    //placed by clicking the map to pick its centre, picking a trip from the list draws its
    //route, and the device list is short enough that blacking out the whole map for it is more
    //than it needs.
    var overMap = ['history', 'geofence', 'account', 'trips'];
    var open = isPanel && overMap.indexOf(hash) === -1;

    if (document.body.classList) {
        document.body.classList.toggle('panelOpen', open);
        document.body.classList.toggle('statsOpen', hash === 'stats');
    }

    if (open) {
        layoutStatsPanel();
    }
}

function renderStatChips() {
    var host = document.getElementById('statsControls');

    if (!host) {
        return;
    }

    var hidden = hiddenStatGroups();

    //before any stats have arrived nothing is known to be present, so offer the lot
    var groups = statGroupsPresent.length
        ? STAT_GROUPS.filter(function(g) { return statGroupsPresent.indexOf(g.key) !== -1; })
        : STAT_GROUPS;

    host.innerHTML = groups.map(function(g) {
        var off = hidden.indexOf(g.key) !== -1 ? ' off' : '';
        return '<button type="button" class="chip' + off + '" onclick="toggleStatGroup(\'' + g.key + '\')">' + g.label + '</button>';
    }).join('');

    layoutStatsPanel();
}

//Picking a moment on the chart moves the marker on the map, but the panel covers the map
//completely, so the only sign anything had happened was the map having jumped by the time you
//next opened it. Drop the panel out of the way long enough to watch the marker settle, then
//bring it back. The fade itself comes from the .5s transition already on section, so the map
//is fully visible for about a second in the middle.
var mapPeekTimer = null;
var MAP_PEEK_MS = 1600;

function peekAtMap() {
    var panel = document.getElementById('stats');

    if (!panel) {
        return;
    }

    panel.classList.add('peek');
    document.body.classList.add('peeking');

    //a second click while the map is showing extends the look rather than cutting it short
    if (mapPeekTimer) {
        clearTimeout(mapPeekTimer);
    }

    mapPeekTimer = setTimeout(function() {
        panel.classList.remove('peek');
        document.body.classList.remove('peeking');
        mapPeekTimer = null;
    }, MAP_PEEK_MS);
}

function makeChart(datasets) {
    var ctx = document.getElementById("lineChart");
    var narrow = narrowScreen();

    renderStatChips();

    var options = {
        type: 'line',
        data: {
            datasets: datasets
        },
        options: {
            //the canvas is sized by its wrapper. left to itself Chart.js holds a 2:1 ratio
            //and threw away the height the page asked for, so on a phone the graph came out
            //about as tall as a line of text.
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            //a finger is not a mouse pointer: without this nothing responds unless the tap
            //lands exactly on a plotted point
            interaction: {
                mode: 'nearest',
                intersect: false
            },
            onClick: function(e, elements, chart) {
                //the event arrives already measured against the canvas, so there is no
                //helper to reach for and no page offsets to subtract
                var dataX = chart.scales.x.getValueForPixel(e.x);
                var moved = false;

                for (var i = 0; i < (historyItems.length - 1); i++) {
                    if (dataX >= historyItems[i][0] && dataX <= historyItems[i + 1][0]) {
                        stepIndex = i;
                        isPlaying = 0;
                        noUpdateCurrentPosition = true;
                        updateMarker(historyItems[i][1], historyItems[i][2], historyItems[i][0], true);
                        moved = true;
                    }
                }

                //the marker moved on a map nobody can see, so show it happening
                if (moved) {
                    peekAtMap();
                }
            },
            plugins: {
                legend: {
                    //A phone needs this more than a desktop does, not less - without it the
                    //lines have no names at all. It is the chips that keep it to a readable
                    //size: with the device group folded away it is seven entries, not twelve.
                    display: true,
                    position: 'top',
                    labels: {
                        color: CHART_INK,
                        boxWidth: narrow ? 8 : 12,
                        padding: narrow ? 6 : 10,
                        usePointStyle: true,
                        font: {
                            size: narrow ? 10 : 12
                        }
                    }
                },
                tooltip: {
                    caretPadding: 8,
                    titleFont: {
                        size: 12
                    },
                    bodyFont: {
                        size: 13
                    },
                    callbacks: {
                        label: function(context) {
                            var unit = context.dataset.unit;
                            var name = context.dataset.label;
                            //trailing zeros on a heart rate of 58.00 read as false precision
                            var value = parseFloat(Number(context.parsed.y).toFixed(2));

                            return unit ? name.replace(' (' + unit + ')', '') + ': ' + value + ' ' + unit
                                        : name + ': ' + value;
                        }
                    }
                }
            },

            scales: {
                x: {
                    type: 'time',
                    display: true,
                    //no fixed unit - a hard coded 'minute' made the labels meaningless
                    //across a 30 day range. the tokens are date-fns ones, which the adapter
                    //this build uses expects - lower case d for the day of the month.
                    time: {
                        tooltipFormat: 'MMM d, HH:mm',
                        displayFormats: {
                            minute: 'HH:mm',
                            hour: 'HH:mm',
                            day: 'MMM d'
                        }
                    },
                    title: {
                        display: false
                    },
                    grid: {
                        color: CHART_GRID
                    },
                    ticks: {
                        autoSkip: true,
                        maxRotation: 0,
                        maxTicksLimit: narrow ? 4 : 9,
                        color: CHART_INK
                    }
                },
                y: {
                    display: true,
                    position: 'left',
                    title: {
                        display: false
                    },
                    grid: {
                        color: CHART_GRID
                    },
                    ticks: {
                        autoSkip: true,
                        maxTicksLimit: narrow ? 6 : 10,
                        color: CHART_INK
                    }
                },
                y2: {
                    //the small ranged series, so they are not flattened against the bottom
                    //of a scale whose top blood pressure sets
                    display: true,
                    position: 'right',
                    beginAtZero: true,
                    title: {
                        display: false
                    },
                    grid: {
                        //only one set of horizontal lines across the plot
                        drawOnChartArea: false,
                        color: CHART_GRID
                    },
                    ticks: {
                        autoSkip: true,
                        maxTicksLimit: narrow ? 6 : 10,
                        color: CHART_INK
                    }
                }
            }
        }
    };

    //The stats refresh every 80 seconds. Rebuilding the chart each time would throw away
    //any series the user had switched off from the legend, so only swap the data when the
    //shape is unchanged - Chart.js keeps the per-series hidden flags across an update. A
    //changed series list or a move between the narrow and wide layouts needs the axes and
    //legend built again.
    var signature = (narrow ? 'n|' : 'w|') + datasets.map(function(d) { return d.label; }).join('|');

    if (lineChart && signature === chartSignature) {
        lineChart.data.datasets = datasets;
        lineChart.update();
        return;
    }

    if (lineChart) {
        lineChart.destroy();
    }

    chartSignature = signature;
    lineChart = new Chart(ctx, options);
}

//crossing between the narrow and wide layouts changes the legend and how hard the series are
//thinned, and neither is something Chart.js re-evaluates on its own
var lastNarrow = null;

window.addEventListener('resize', function() {
    var now = narrowScreen();

    //the range pickers wrap differently as the width changes, so the space held for them has
    //to be measured again whether or not the chart itself needs rebuilding
    layoutStatsPanel();

    if (lastNarrow !== null && now !== lastNarrow && lineChart) {
        makeChart(makeDataset(statsShown));
    }

    lastNarrow = now;
});

function fetchStats() {
    $.ajax({
        url: constructURL("stats.php", arrayBeginDate(statsList), getSelectedEndDate()),
        success: function(result) {
            var parsed = forEachRow(result, 2, cols => [new Date(cols[0]), cols[1], parseFloat(cols[2])]);

            if (parsed.length == 0) {
                return;
            }

            statsList = statsList.concat(parsed);

            if (parsed.length) {

                makeChart(makeDataset(statsList));
            }
        }
    });
}


function filterStats(beginDate, endDate) {
    var filtered = statsList.filter(cols => cols[0] >= beginDate && cols[0] <= endDate);
    makeChart(makeDataset(filtered));
}


function findStat(name, dt) {
    var dataItems = statsList.filter(x => x[1] == name);
    for (var idx = 0; idx < dataItems.length; idx++) {
        if (idx == dataItems.length - 1) {
            return dataItems[idx][2];
        }
        if (dataItems[idx][0] < dt && dataItems[idx + 1][0] >= dt) {
            return dataItems[idx + 1][2];
        }
    }

    return 0;
}

function setMarker(lat, lng, move, text) {
    pointFeature.name = text;
    pointFeature.getGeometry().setCoordinates(ol.proj.fromLonLat([lng, lat]));

    if (move) {
        animateTo(lng,lat);
    }
}

function setBattery(batlvl) {
    if (batlvl >= 75) {
        document.getElementById("batt").className = "icon battery-full";
    } else if (batlvl >= 51) {
        document.getElementById("batt").className = "icon battery-three-quarters";
    } else if (batlvl >= 25) {
        document.getElementById("batt").className = "icon battery-half";
    } else if (batlvl >= 10) {
        document.getElementById("batt").className = "icon battery-quarter";
    } else {
        document.getElementById("batt").className = "icon battery-empty";
    }
}

function setSignal(signal) {
    if (signal >= 75) {
        document.getElementById("signal").className = "icon signal";
    } else if (signal >= 50) {
        document.getElementById("signal").className = "icon signal-4";
    } else if (signal >= 25) {
        document.getElementById("signal").className = "icon signal-3";
    } else if (signal >= 10) {
        document.getElementById("signal").className = "icon signal-2";
    } else {
        document.getElementById("signal").className = "icon signal-1";
    }
}

function updateSpeed(spd) {
    var speedDiv = document.getElementById("speed");
    speedDiv.innerHTML = '  speed: ' + speedText(spd);
}

/*
 * findStat() hands back the nearest value and nothing about when it was taken, so it cannot say
 * whether a reading is current or hours old - and it falls back to the newest reading it has
 * whatever its date. A pulse written next to a position has to be one that was measured near
 * it, or the marker states a heart rate for a moment nobody measured one.
 */
var HEARTRATE_FRESH_MS = 30 * 60 * 1000;
//a speed is written with every position, so anything much older than the idle reporting
//interval belongs to a different moment than the one being shown
var SPEED_FRESH_MS = 15 * 60 * 1000;

function statAt(name, dt, freshMs) {
    var best = null;

    for (var i = 0; i < statsList.length; i++) {
        if (statsList[i][1] !== name || statsList[i][0] > dt) {
            continue;
        }

        if (!best || statsList[i][0] > best[0]) {
            best = statsList[i];
        }
    }

    //null rather than 0: a speed of zero is a device standing still, which is a different
    //statement from having no reading to show
    if (!best) {
        return null;
    }

    return (dt - best[0]) <= freshMs ? best[2] : null;
}

function updateMarker(lat, lng, dt, forceMove = false) {
    var spd = findStat('speed', dt);
    var batlvl = findStat('battery_level', dt);
    var signal = findStat('signal', dt);
    signal = signal ? signal : 100;

    setBattery(batlvl);
    setSignal(signal);

    var bpm = statAt('heartrate', dt, HEARTRATE_FRESH_MS);
    var labelSpeed = statAt('speed', dt, SPEED_FRESH_MS);

    //Written under the pin as well as in the popup, so both can be read without opening
    //anything. Either half is left out when there is nothing to say - a tower fix measures no
    //speed, and a tracker without a sensor reports no pulse.
    var label = [];

    if (labelSpeed !== null) {
        label.push(labelSpeed.toFixed(1) + ' km/h');
    }

    if (bpm !== null) {
        label.push(Math.round(bpm) + ' bpm');
    }

    pointFeature.setStyle(markerStyleWithLabel(label));

    var moveMarker = forceMove || (new Date().getTime() - newImei) < 30000;
    setMarker(lat, lng, moveMarker, 'Last seen on: ' + readableDate(dt) + ' <br>Moving at speed ' + spd + 'km/h<br><br>'
        + (bpm !== null ? 'Heart rate: ' + Math.round(bpm) + ' bpm<br>' : '')
        + 'Battery: ' + batlvl + '%<br>' + 'Gsm signal strength: ' + signal + '%<br>');
    updateSpeed(spd);
}

var noUpdateCurrentPosition=false;
function updateCurrentPosition(force = false) {
    if (imei == null || imei == '' || noUpdateCurrentPosition)
        return;

    if (historyItems.length && !force) {
        var dt = historyItems[historyItems.length - 1][0];
        var lat = historyItems[historyItems.length - 1][1];
        var lng = historyItems[historyItems.length - 1][2];
        updateMarker(lat, lng, dt);
    } else {
        var url = "current.php?imei=" + imei + viewOnlyParameter();
        $.ajax({
            url: url,
            type: 'GET',
            cache: false,
            success: function(result) {
                var lines = result.split(/\r?\n/);
                var coords = lines[0].split(/,/);
                var misc = lines[1].split(/,/);

                if (coords.length < 4) return;

                updateMarker(coords[1], coords[2],new Date(coords[0]),force);

                setBattery(misc[0]);
                setSignal(misc[1]);
                updateSpeed(coords[3]);
            }
        });
    }
}

function refreshData() {
    fetchFence();
    refreshSettings();
    if (!tripActive && (getSelectedEndDate() >= new Date() || !historyItems.length)) {
        fetchEvents();
        fetchCommandResults()
        fetchStats();
        fetchHistory();
        fetchLogging();
    }
}

/*
 * How far back the range the page picks for itself starts: half the width it is about to be.
 *
 * This was a flat 720 minutes, which is half of the twenty four hours the count starts on - so
 * the page opened showing the last twelve hours with twelve hours of room ahead of it, and
 * refreshData() collected into that room until it ran out. That is the shape the whole range
 * mechanism assumes: refreshData gives up entirely once the selected end is in the past, so a
 * window has to reach into the future to stay live at all.
 *
 * Being flat, it only held for twenty four. Choosing any other duration left the start where it
 * was and moved only the end, so:
 *
 *   12h  ->  begin now-12h, end now      live for no time at all; the last hour never arrives
 *    6h  ->  begin now-12h, end now-6h   ends six hours ago, dead on selection
 *    1h  ->  begin now-12h, end now-11h  one hour, from half a day ago
 *
 * Half the selected width is the same rule the default always followed, applied to the width
 * actually selected: history behind, as much room ahead, and rollAutoRange to move it on when
 * that room is used up.
 */
function selectedHours() {
    var el = document.getElementById('hourCount');
    var v = el ? parseFloat(el.value) : 24;
    return isFinite(v) && v > 0 ? v : 24;
}

function autoRangeMinutes() {
    return selectedHours() * 30;
}

/*
 * Whether the range is still the one the page chose, rather than one the user asked for.
 *
 * The controls are filled in at load with a window that starts twelve hours ago, so with the
 * default twenty four hour count it ends twelve hours from now. refreshData() collects new
 * readings into it until then and stops: its own guard gives up once the selected end is in
 * the past. A tab left open overnight went quiet at that point and the graph stopped where the
 * window ended, which looks exactly like the device having gone quiet.
 *
 * Moving the window on once it has expired fixes that, and only while the user has not touched
 * the controls - someone who has deliberately gone back to look at last Tuesday should not have
 * it taken away from them.
 *
 * This replaces a version that compared the date box against today and rolled it over at
 * midnight. It could not work here, because setBeginDate() had already written twelve hours ago
 * into that box: before noon that is yesterday, so the comparison read every morning load as a
 * date the user had chosen and nothing ever rolled. On an afternoon load, where it did match,
 * rolling the date while the time box held a wall clock time moved the start of the window a day
 * forward rather than up to now - so at midnight the range began hours in the future and the
 * page went blank.
 */
var autoRange = true;

/*
 * The three controls in tracker.php call this rather than searchdateChange directly, so that
 * handing control over is something the user does by touching one of them. Working it out
 * afterwards by comparing values is what the version above got wrong.
 */
function rangeEdited() {
    autoRange = false;
    searchdateChange();
}

/*
 * The duration control is a width, not a start.
 *
 * It called rangeEdited() with the other two, which handed the range over to the user and left
 * the start where it was - so asking for a shorter window asked for a window ending further in
 * the past, and asking for six hours showed six hours from half a day ago. Choosing how much to
 * look at is not the same as choosing when to look at, so this re-anchors on now and stays in
 * automatic. The date and time boxes still hand over: someone who has typed last Tuesday into
 * them means it.
 */
function durationEdited() {
    autoRange = true;
    setBeginDate(autoRangeMinutes());
    searchdateChange();
}

function rollAutoRange() {
    //nothing to do while the window still reaches into the future: refreshData() is already
    //collecting into it, and moving it would only throw away what it has collected
    if (!autoRange || getSelectedEndDate() >= new Date()) {
        return;
    }

    setBeginDate(autoRangeMinutes());
    searchdateChange();
}

document.addEventListener('DOMContentLoaded', function () {
    //a minute is plenty: the window is twelve hours wide and nothing here needs to notice its
    //end to the second
    setInterval(rollAutoRange, 60000);
});

/*
 * Draw the emptied lists, so a new range starts from a blank view.
 *
 * searchdateChange throws the data arrays away, but nothing redraws from them: every fetch
 * below renders only inside its success handler, and each one returns early when the server
 * sends back no rows. A date the device has no data for - anything before it first reported,
 * which is most of the calendar - therefore left the previous range's track, chart, tables
 * and distance sitting on screen, and the page looked like it had ignored the date entirely.
 *
 * Rendering here instead of trusting the fetches means an empty range shows as empty, and a
 * range with data has it drawn over this a moment later.
 */
function clearView() {
    eventLayer.getSource().clear();
    document.getElementById("alarmBody").innerHTML = '';
    document.getElementById("commandBody").innerHTML = '';
    document.getElementById("serverLoggingBody").innerHTML = '';
    refreshHistory();
    refreshTrips();
    makeChart(makeDataset(statsList));
}

function searchdateChange() {
    tripActive = false;
    eventList = [];
    statsList = [];
    historyItems = [];
    serverLogging = [];
    commandResults = [];
    unfilteredHistory = [];
    clearView();
    refreshData();
}

//the chips label a panel that may be opened before any stats have arrived, and makeChart -
//which normally draws them - is never reached when a device has no readings yet
renderStatChips();
layoutStatsPanel();
syncStatsMode();
window.addEventListener('hashchange', syncStatsMode);

/*
 * Panels are shown with :target, so opening one is a hash change and every one opened pushed a
 * history entry. Back then walked backwards through every panel visited, one press at a time,
 * rather than leaving the page - which on a phone is the whole back gesture spent undoing
 * navigation the user did not think of as navigation.
 *
 * Opening the first panel still pushes, so back closes it, which is what the gesture is for.
 * Moving from one open panel to another replaces the entry instead. location.replace is what
 * does that rather than history.replaceState: :target is only re-evaluated by an actual
 * navigation, and replaceState is not one - the URL would change and the panel would not.
 */
/*
 * Escape closes whichever right hand panel is open, the same as pressing back does. A panel is
 * shown by the hash naming a section, and opening the first one pushed a history entry (see the
 * click handler below), so history.back() is exactly the "close it" step - it drops that entry
 * and the hash with it. Only when a panel is actually open, or Escape on the bare map would walk
 * off the page. A field or a dropdown having focus is left alone: Escape there is the browser's.
 */
document.addEventListener('keydown', function(e) {
    var focused = document.activeElement;

    //never steal a key while a field or dropdown has focus - Escape and '=' both belong to it
    if (focused && /^(INPUT|SELECT|TEXTAREA)$/.test(focused.tagName)) {
        return;
    }

    //'=' zooms in, matching the '+' openlayers already binds. On a mac keyboard '+' needs shift,
    //so '=' is the key actually under the finger - the same one the browser uses for its own
    //zoom-in. The map's built in '-' handles zooming out.
    if (e.key === '=' || e.key === '+' || e.keyCode === 187) {
        e.preventDefault();
        var view = map.getView();
        view.animate({ zoom: Math.min(view.getMaxZoom(), view.getZoom() + 1), duration: 200 });
        return;
    }

    if (e.key !== 'Escape' && e.keyCode !== 27) {
        return;
    }

    var hash = window.location.hash.replace('#', '');
    var target = hash ? document.getElementById(hash) : null;

    if (target && target.tagName === 'SECTION') {
        e.preventDefault();

        //back is the intended close. But if the page was opened straight onto a panel hash -
        //a reload while one was open - there may be no entry behind it, and back would leave
        //the page. So if the panel is still open a moment later, close it outright instead.
        history.back();

        setTimeout(function() {
            if (window.location.hash.replace('#', '') === hash) {
                window.location.replace(window.location.pathname + window.location.search);
            }
        }, 120);
    }
});

document.addEventListener('click', function(e) {
    if (!e.target || !e.target.closest) {
        return;
    }

    var link = e.target.closest('nav a[href^="#"]');

    if (!link) {
        return;
    }

    var current = window.location.hash.replace('#', '');
    var target = current ? document.getElementById(current) : null;

    //nothing open yet: let this one push, so there is something for back to close
    if (!target || target.tagName !== 'SECTION') {
        return;
    }

    var href = link.getAttribute('href');
    e.preventDefault();
    window.location.replace(href === '#' ? window.location.pathname + window.location.search : href);
});

setTimeout(updateCurrentPosition, 500);
setInterval(updateCurrentPosition, 10000);
setInterval(refreshData, 80000);
setInterval(fetchLogging, 280000);

setBeginDate(autoRangeMinutes());

function setBeginDate(offsetMinutes) {
    var dateOffset = (60 * offsetMinutes * 1000);
    var date = new Date();
    date.setTime(date.getTime() - dateOffset);
    var seconds = String(date.getSeconds());
    var minutes = String(date.getMinutes());
    var hour = String(date.getHours());
    var year = String(date.getFullYear());
    var month = String(date.getMonth() + 1); // beware: January = 0; February = 1, etc.
    var day = String(date.getDate());

    document.getElementById("beginDate").value = year + "-" + month.padStart(2, '0') + "-" + day.padStart(2, '0');
    document.getElementById("beginTime").value = hour.padStart(2, '0') + ":" + minutes.padStart(2, '0') + ":" + seconds.padStart(2, '0');
}

var moveSpeed=500;


function moveStep() {
    setTimeout(moveStep, moveSpeed);

    if (historyItems.length == 0) return;

    if (isPlaying) {
        if (stepIndex >= historyItems.length) {
            isPlaying = 0;
            stepIndex = 0;
        }
        var dt = historyItems[stepIndex][0];
        var lat = historyItems[stepIndex][1];
        var lng = historyItems[stepIndex][2];
        updateMarker(lat, lng, dt, true);
        stepIndex++;
    }
}

setTimeout(moveStep, moveSpeed);

function startPlaying() {
    isPlaying = 1;
    noUpdateCurrentPosition=true;
}

function stopPlaying() {
    stepIndex = 0;
    isPlaying = 0;
}

function pausePlaying() {
    isPlaying = 0;
}

function recenter() {
    stopPlaying();
    exitTrip();
    noUpdateCurrentPosition=false;
    updateCurrentPosition(true);
}

function playFaster(){
    moveSpeed/=1.5;
    animateSpeed=moveSpeed/1.5;
}

function playSlower(){
    moveSpeed*=1.5;
    animateSpeed=moveSpeed/1.5;
}

map.on('singleclick', function(evt) {
    var name = '';

    var fenceLat = document.getElementById("fenceLat");
    var fenceLong = document.getElementById("fenceLong");
    var lonlat = ol.proj.transform(evt.coordinate, 'EPSG:3857', 'EPSG:4326');

    //Only while the geofence panel is open. A click anywhere on the map used to move the fence
    //position whatever the user was actually doing, which was invisible before there was a
    //preview and would now put a grey circle on the map while they were reading a trip.
    //Each click moves the preview, so a fence can be aimed before anything is added.
    if (fenceLat && fenceLong && window.location.hash === '#geofence') {
        fenceLat.value = lonlat[1];
        fenceLong.value = lonlat[0];
        moveDemoFeature();
    }

    var hasFeature=false;
    map.forEachFeatureAtPixel(evt.pixel, function(feature) {
        hasFeature=true;
        if (!!feature.name)
            name = feature.name;

        if (!!feature.CAPTION)
            name = feature.CAPTION;
    })


    if (name != '') {
        container.style.display = "block";
        var coordinate = evt.coordinate;
        content.innerHTML = name;
        overlay.setPosition(coordinate);
    } else {
        if(hasFeature){
            var nearestItem=null;

            var closestDistance=Number.MAX_VALUE;

            for(var i=0;i<historyItems.length;i++){
                var newDistance=haversineDistance(lonlat[1],lonlat[0],historyItems[i][1],historyItems[i][2]);
                if(newDistance<closestDistance){
                    nearestItem=i;
                    closestDistance=newDistance;
                }
            }

            if(nearestItem){
                noUpdateCurrentPosition=true;
                isPlaying=false;
                stepIndex=nearestItem;
                updateMarker(historyItems[nearestItem][1], historyItems[nearestItem][2], historyItems[nearestItem][0], true);
            }
        }
        //show marker at current click point, and update index to click point
        container.style.display = "none";
    }
});
