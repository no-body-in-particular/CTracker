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

function computeEventRow(cols) {
    return "<tr onclick='animateTo(" + cols[2] + "," + cols[1] + ")'><td>" + readableDate(new Date(cols[0])) + "</td><td>" + cols[3] + "</td><td>" + cols[4] + "</td></tr>";
}

function computeHistoryRow(cols) {
    return "<tr onclick='animateTo(" + cols[2] + "," + cols[1] + ")'><td>" + readableDate(new Date(cols[0])) + "</td><td>" + cols[1] + "</td><td>" + cols[2] + "</td><td>" + cols[3] + "</td></tr>";
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
            var features = eventList.map(rv => {
                var f = new ol.Feature(new ol.geom.Circle(ol.proj.fromLonLat([rv[2], rv[1]]), 5));
                f.EVT = rv[4];
                f.CAPTION = (rv[4] + ' on ' + readableDate(rv[0]) + ' while moving at speed: ' + rv[3]);
                lastCaption = f.CAPTION;
                return f;
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
    var url = "geofence.php?imei=" + imei + "&action=write&fence=" + encodeURIComponent(cols) + "&username=" + document.getElementById("username").value + "&password=" +
        document.getElementById("password").value;

    copyTextToClipboard(url);

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
    geofenceLayer.getSource().forEachFeature(function(feature) {
        if (feature.TYPE == 'demo') {
            feature.setGeometry(new ol.geom.Circle(ol.proj.fromLonLat([document.getElementById("fenceLong").value, document.getElementById("fenceLat").value]), document.getElementById("fenceRadius").value * 1.60934));
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

            var f = new ol.Feature(new ol.geom.Circle(ol.proj.fromLonLat([0, 0]), 100 * 1.60934));
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

var stepIndex = 0;
var isPlaying = 0;

function makeChart(datasets) {
    var ctx = document.getElementById("lineChart");
    var options = {
        type: 'line',
        data: {
            datasets: datasets
        },
        options: {
            onClick: (e) => {
                const canvasPosition = Chart.helpers.getRelativePosition(e, lineChart);
                // Substitute the appropriate scale IDs
                const dataX = lineChart.scales.x.getValueForPixel(canvasPosition.x);
                for (var i = 0; i < (historyItems.length - 1); i++) {
                    if (dataX >= historyItems[i][0] && dataX <= historyItems[i + 1][0]) {
                        stepIndex=i;
                        isPlaying=0;
                        noUpdateCurrentPosition=true;
                        updateMarker(historyItems[i][1], historyItems[i][2], historyItems[i][0], true);
                    }
                }
            },
            plugins: {
                legend: {
                    position: 'top',
                }
            },

            scales: {
                xAxes: [{
                    type: 'time',
                    display: true,
                    time: {
                        unit: 'minute'
                    },
                    scaleLabel: {
                        display: true,
                        labelString: 'Date'
                    },
                    ticks: {
                        autoSkip: true,
                        major: {
                            fontStyle: 'bold',
                            fontColor: '#FF0000'
                        }
                    },
                    id: 'x'
                }],
                yAxes: [{
                    display: true,
                    scaleLabel: {
                        display: true,
                        labelString: 'value'
                    },
                    ticks: {
                        autoSkip: true
                    },
                    id: 'y'
                }]
            }
        }
    };

    if (!lineChart) {
        lineChart = new Chart(ctx, options);
    } else {
        lineChart.data.datasets = datasets;
        lineChart.update();
    }
}


var statsList = [];


function makeDataset(itemList) {
    var types = itemList.map(x => x[1]);
    types = [...new Set(types)];

    return types.map(type => {
        var dataItems = itemList.filter(x => x[1] == type);

        if (dataItems.length) {
            var c1 = 0;
            for (let i = 0; i < type.length; i++) {
                c1 += type.charCodeAt(i);
            }

            var fillcolor = 'rgba(' + (Math.floor(c1) % 10) * 25 + ', ' + (Math.floor(c1 / 100) % 10) * 25 + ', ' + (Math.floor(c1 / 10) % 10) * 25 + ', 1)';
            var noFill = 'rgba(' + (Math.floor(c1) % 10) * 25 + ', ' + (Math.floor(c1 / 100) % 10) * 25 + ', ' + (Math.floor(c1 / 10) % 10) * 25 + ', 0.1)';


            var newDataset = {
                label: type,
                backgroundColor: noFill,
                borderColor: fillcolor,
                borderWidth: 1,
                data: itemList.filter(x => x[1] == type).map(x => {
                    return {
                        "x": x[0],
                        "y": x[2]
                    };
                })
            }

            return newDataset;
        }
    });
}


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
    speedDiv.innerHTML = '  speed: ' + spd + 'km/h';
}

function updateMarker(lat, lng, dt, forceMove = false) {
    var spd = findStat('speed', dt);
    var batlvl = findStat('battery_level', dt);
    var signal = findStat('signal', dt);
    signal = signal ? signal : 100;

    setBattery(batlvl);
    setSignal(signal);

    var moveMarker = forceMove || (new Date().getTime() - newImei) < 6000;
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

                var moveMarker = force || (new Date().getTime() - newImei) < 6000;
                setMarker(coords[1], coords[2], moveMarker, 'Last seen on: ' + readableDate(new Date(coords[0])) + ' <br>Moving at speed ' + coords[3] + 'km/h<br><br>' + 'Battery: ' + misc[0] + '%<br>' + 'Gsm signal strength: ' + misc[1] + '%<br>');
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

setInterval(updateCurrentPosition, 10000);
setInterval(refreshData, 80000);
setInterval(fetchLogging, 280000);

setBeginDate(120);

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
