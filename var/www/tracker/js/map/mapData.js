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

function computeEventRow(cols) {
    return "<tr onclick='animateTo(" + cols[2] + "," + cols[1] + ")'><td>" + readableDate(new Date(cols[0])) + "</td><td>" + speedText(cols[3]) + "</td><td>" + cols[4] + "</td></tr>";
}

function computeHistoryRow(cols) {
    return "<tr onclick='animateTo(" + cols[2] + "," + cols[1] + ")'><td>" + readableDate(new Date(cols[0])) + "</td><td>" + cols[1] + "</td><td>" + cols[2] + "</td><td>" + speedText(cols[3]) + "</td></tr>";
}

function computeLogRow(cols) {
    return "<tr><td>" + readableDate(new Date(cols[0])) + "</td><td style='font-size:10px'>" + cols[1] + "</td></tr>";
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


    return "<tr onclick='animateTo(" + cols[5] + "," + cols[4] + ")'><td>" + localTime(cols[0])[1] + "</td><td>" + localTime(cols[1])[1] + "</td><td>" + dayOfWeek[displayDate] + "</td><td>" + fenceType[cols[3]] + "</td><td>" + cols[6] + "m</td><td>" + alarmEnabled[cols[7]] + "</td><td>" + cols[8] + "</td><td><button onClick='deleteFence(\"" + cols.join(',') + "\")' >delete</button></td></tr>";
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

            eventList = eventList.concat(parsed);

            var lastCaption;
            var features = [];
            eventList.forEach(rv => {
                var caption = (rv[4] + ' on ' + readableDate(rv[0]) + ' while moving at speed: ' + speedText(rv[3]));
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
            tableBody.innerHTML = eventList.reverse().map(rv => computeEventRow(rv)).join('');
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
            tableBody.innerHTML = serverLogging.reverse().map(rv => computeLogRow(rv)).join('');

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

            commandResults = commandResults.concat(parsed);

            const tableBody = document.getElementById("commandBody");
            tableBody.innerHTML = commandResults.reverse().map(rv => computeLogRow(rv)).join('');
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

function moveDemoFeature() {
    var lng = parseFloat(document.getElementById("fenceLong").value);
    var lat = parseFloat(document.getElementById("fenceLat").value);
    var radius = parseFloat(document.getElementById("fenceRadius").value);

    geofenceLayer.getSource().forEachFeature(function(feature) {
        if (feature.TYPE == 'demo') {
            //the lat/long inputs are hidden and start at 0, so until a fence has actually
            //been placed on the map there is no position to preview. clear the geometry
            //rather than draw the preview in the gulf of guinea.
            if (!isFinite(lat) || !isFinite(lng) || !isFinite(radius) || (lat === 0 && lng === 0)) {
                feature.setGeometry(null);
                return;
            }

            feature.setGeometry(new ol.geom.Circle(ol.proj.fromLonLat([lng, lat]), radius * 1.60934));
        }
    });
}

function fetchFence() {
    $.ajax({
        url: "geofence.php?imei=" + imei + viewOnlyParameter(),
        success: function(result) {
            enableDownload(result, false);
            var parsed = forEachRow(result, 8, cols => [cols[0], cols[1], cols[2], cols[3], parseFloat(cols[4]), parseFloat(cols[5]), parseFloat(cols[6]), cols[7], cols[8]]);


            var coords = parsed.map(rv => {
                var f = new ol.Feature(new ol.geom.Circle(ol.proj.fromLonLat([rv[5], rv[4]]), rv[6] * 1.60934));
                f.TYPE = rv[3];
                return f;
            });

            //preview circle for the geofence editor. it is created with no geometry so it
            //draws nothing until moveDemoFeature() places it. it used to be built at 0,0,
            //which parked a permanent 160km grey circle on null island off west africa for
            //anyone who had not yet placed a fence.
            var f = new ol.Feature();
            f.TYPE = 'demo';

            coords.push(f);

            //new ol.Feature(new ol.geom.Circle(centerLongitudeLatitude, 4000))]
            geofenceLayer.getSource().clear();
            geofenceLayer.getSource().addFeatures(coords);

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
    battery_level: { group: 'device',   axis: 'y',  colour: '#9d8df1', label: 'battery',       unit: '%' },
    signal:        { group: 'device',   axis: 'y',  colour: '#6c8cff', label: 'gsm signal',    unit: '' },
    gps_sats:      { group: 'device',   axis: 'y2', colour: '#c8b6ff', label: 'gps satellites', unit: '' },
    wifi_networks: { group: 'device',   axis: 'y2', colour: '#7bdff2', label: 'wifi networks', unit: '' },
    lbs_stations:  { group: 'device',   axis: 'y2', colour: '#b0b6c8', label: 'cell towers',   unit: '' }
};

var STAT_GROUPS = [
    { key: 'vitals',   label: 'Vitals' },
    { key: 'activity', label: 'Activity' },
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

function breakGaps(points) {
    var out = [];

    for (var i = 0; i < points.length; i++) {
        if (i > 0 && (points[i].x - points[i - 1].x) > STAT_GAP_MS) {
            out.push({ x: new Date(points[i - 1].x.getTime() + 1), y: null });
        }

        out.push(points[i]);
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
            lineTension: 0,
            fill: false,
            spanGaps: false,
            data: breakGaps(decimate(points, budget))
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
    var open = !!(target && target.tagName === 'SECTION');

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
            animation: { duration: 0 },
            onClick: (e) => {
                const canvasPosition = Chart.helpers.getRelativePosition(e, lineChart);
                // Substitute the appropriate scale IDs
                const dataX = lineChart.scales.x.getValueForPixel(canvasPosition.x);
                var moved = false;
                for (var i = 0; i < (historyItems.length - 1); i++) {
                    if (dataX >= historyItems[i][0] && dataX <= historyItems[i + 1][0]) {
                        stepIndex=i;
                        isPlaying=0;
                        noUpdateCurrentPosition=true;
                        updateMarker(historyItems[i][1], historyItems[i][2], historyItems[i][0], true);
                        moved = true;
                    }
                }

                //the marker moved on a map nobody can see, so show it happening
                if (moved) {
                    peekAtMap();
                }
            },
            //a finger is not a mouse pointer: without this nothing responds unless the tap
            //lands exactly on a plotted point
            hover: {
                mode: 'nearest',
                intersect: false
            },
            tooltips: {
                mode: 'nearest',
                intersect: false,
                caretPadding: 8,
                titleFontSize: 12,
                bodyFontSize: 13,
                callbacks: {
                    label: function(item, data) {
                        var set = data.datasets[item.datasetIndex];
                        //trailing zeros on a heart rate of 58.00 read as false precision
                        var value = parseFloat(Number(item.yLabel).toFixed(2));
                        return set.unit ? set.label.replace(' (' + set.unit + ')', '') + ': ' + value + ' ' + set.unit
                                        : set.label + ': ' + value;
                    }
                }
            },
            legend: {
                //A phone needs this more than a desktop does, not less - without it the lines
                //have no names at all. It is the chips that keep it to a readable size: with
                //the device group folded away it is seven entries, not twelve.
                display: true,
                position: 'top',
                labels: {
                    fontColor: CHART_INK,
                    boxWidth: narrow ? 8 : 12,
                    fontSize: narrow ? 10 : 12,
                    padding: narrow ? 6 : 10,
                    usePointStyle: true
                }
            },

            scales: {
                xAxes: [{
                    type: 'time',
                    display: true,
                    //no fixed unit - a hard coded 'minute' made the labels meaningless
                    //across a 30 day range
                    time: {
                        tooltipFormat: 'MMM D, HH:mm',
                        displayFormats: {
                            minute: 'HH:mm',
                            hour: 'HH:mm',
                            day: 'MMM D'
                        }
                    },
                    scaleLabel: {
                        display: false
                    },
                    gridLines: {
                        color: CHART_GRID,
                        zeroLineColor: CHART_GRID
                    },
                    ticks: {
                        autoSkip: true,
                        maxRotation: 0,
                        maxTicksLimit: narrow ? 4 : 9,
                        fontColor: CHART_INK,
                        major: {
                            fontStyle: 'bold',
                            fontColor: CHART_INK
                        }
                    },
                    id: 'x'
                }],
                yAxes: [{
                    display: true,
                    position: 'left',
                    scaleLabel: {
                        display: false
                    },
                    gridLines: {
                        color: CHART_GRID,
                        zeroLineColor: CHART_GRID
                    },
                    ticks: {
                        autoSkip: true,
                        maxTicksLimit: narrow ? 6 : 10,
                        fontColor: CHART_INK
                    },
                    id: 'y'
                }, {
                    //the small ranged series, so they are not flattened against the bottom
                    //of a scale whose top blood pressure sets
                    display: true,
                    position: 'right',
                    scaleLabel: {
                        display: false
                    },
                    gridLines: {
                        drawOnChartArea: false,
                        color: CHART_GRID
                    },
                    ticks: {
                        autoSkip: true,
                        maxTicksLimit: narrow ? 6 : 10,
                        fontColor: CHART_INK,
                        beginAtZero: true
                    },
                    id: 'y2'
                }]
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

function updateMarker(lat, lng, dt, forceMove = false) {
    var spd = findStat('speed', dt);
    var batlvl = findStat('battery_level', dt);
    var signal = findStat('signal', dt);
    signal = signal ? signal : 100;

    setBattery(batlvl);
    setSignal(signal);

    var moveMarker = forceMove || (new Date().getTime() - newImei) < 30000;
    console.log(moveMarker);
    setMarker(lat, lng, moveMarker, 'Last seen on: ' + readableDate(dt) + ' <br>Moving at speed ' + spd + 'km/h<br><br>' + 'Battery: ' + batlvl + '%<br>' + 'Gsm signal strength: ' + signal + '%<br>');
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

function searchdateChange() {
    tripActive = false;
    eventList = [];
    statsList = [];
    historyItems = [];
    serverLogging = [];
    commandResults = [];
    unfilteredHistory = [];
    refreshData();
}

//the chips label a panel that may be opened before any stats have arrived, and makeChart -
//which normally draws them - is never reached when a device has no readings yet
renderStatChips();
layoutStatsPanel();
syncStatsMode();
window.addEventListener('hashchange', syncStatsMode);

setTimeout(updateCurrentPosition, 500);
setInterval(updateCurrentPosition, 10000);
setInterval(refreshData, 80000);
setInterval(fetchLogging, 280000);

setBeginDate(720);

function setBeginDate(offsetMinutes) {
    var dateOffset = (60 * offsetMinutes * 1000); //5 days
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

    if (fenceLat && fenceLong) {
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
