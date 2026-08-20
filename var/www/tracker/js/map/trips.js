//you can determine how sensitive our trip detection is here
//set the parameters too small and you'll have lots of false positives because GPS tends to wander
//set them too big and you won't detect short trips.

const maxWanderDistance = 0.10; //maximum distance around a center point a user can wander before a new trip is started
const minAverageTime = 140000; //time over which to compute a center point for a stop
//A stop used to be "under 500m covered in the averaging window", which over 140 seconds is
//12.8 km/h - faster than most people run. A run was therefore read as a series of stops and
//every trip was cut short at the first one. What makes something a stop is not the distance
//but the pace, and the window is not a fixed length either: the reporting interval stretches
//to ten minutes once the device settles, so the same distance can span 140 seconds or 600.
//Measure the pace over whatever the window actually turned out to be.
const stopSpeedKmh = 2.0; //below an amble. a walk is about 5, a run about 9
const minTripDuration = 120000; //maximum distance traveled over minstopDuration
const minTripDance = 0.5; //minimum distance of a trip

//determine if there is a trip starting at this row. a trip is a trip if after a time of averaging points the user never gets back in the wandering radius
function isTripStart(rows, rowIndex) {
    var latSum = 0;
    var longSum = 0;
    var loopCount = 0;

    for (var i = rowIndex + 1; i < rows.length && i > 0; i--) {
        latSum += rows[i][1];
        longSum += rows[i][2];
        loopCount++;

        if (Math.abs(rows[rowIndex][0] - rows[i][0]) > minAverageTime && haversineDistance(latSum / loopCount, longSum / loopCount, rows[i][1], rows[i][2]) < maxWanderDistance) {
            return false;
        }

        if (Math.abs(rows[rowIndex][0] - rows[i][0]) > (minAverageTime * 2)) {
            return true;
        }
    }

    return true;
}

//determines if a point is at a stop. same as above this depends on the time over which points are averaged - if this radius is not left for a while this point is definitely a stop.
function isStop(rows, index, backwards) {
    var latAvg = 0;
    var longAvg = 0;
    var loopCount = 0;
    var distanceTraveled = 0;
    var step = backwards ? -1 : 1;
    var furthest = index;

    for (var n = index; n >= 0 && n < rows.length; n += step) {
        latAvg += rows[n][1];
        longAvg += rows[n][2];
        loopCount++;

        var next = n + step;

        //rows[n + 1] was read regardless of which way the walk was going, so walking forwards
        //off the end of the array produced a NaN distance that then failed every comparison
        if (next < 0 || next >= rows.length) {
            break;
        }

        distanceTraveled += haversineDistance(rows[n][1], rows[n][2], rows[next][1], rows[next][2]);
        furthest = next;

        if (Math.abs(rows[index][0] - rows[n][0]) > minAverageTime) {
            break;
        }
    }

    if (loopCount < 2) {
        return false;
    }

    latAvg = latAvg / loopCount;
    longAvg = longAvg / loopCount;

    var hours = Math.abs(rows[index][0] - rows[furthest][0]) / 3600000;

    //no elapsed time is no evidence either way, and calling it a stop would end a trip on the
    //strength of nothing
    if (hours <= 0) {
        return false;
    }

    return haversineDistance(latAvg, longAvg, rows[index][1], rows[index][2]) < (maxWanderDistance * 2)
        && (distanceTraveled / hours) < stopSpeedKmh;
}

//function that begins a trip at the previous stop
function walkToStop(rows, index, backwards) {
    var latAvg = 0;
    var longAvg = 0;

    for (var i = index - 1; i > 0 && i < rows.length; i += (backwards ? -1 : 1)) {
        if (isStop(rows, i, !backwards)) return i;
    }
    return index;
}

//convert a set of points to x-y coordinates for our map
function pointsToMap(rows, beginIndex, endIndex) {
    var coords = [];
    for (var i = beginIndex; i < (rows.length - 1) && i < endIndex; i++) {
        coords.push(ol.proj.fromLonLat([rows[i][2], rows[i][1]]));
    }
    return coords;
}

var unfilteredHistory = [];

function convert_to_trips(table, rows) {
    var inTrip = false;
    var tripDistance = 0;
    var tripBeginDate;
    var tripBeginIndex;
    var tripBeginCoordinates;
    var tripEndIndex;
    var coords = [];
    var distanceDiv = document.getElementById("distance");

    const tripBeforeAfter = 2;

    if( ! rows || rows.length <2) return;

    for (var i = 0; i < (rows.length - 1); i++) {
        if (!inTrip && isTripStart(rows, i) && !isStop(rows, i, true)) {
            tripBeginIndex = walkToStop(rows, i, true);

            tripBeginDate = rows[tripBeginIndex][0];
            tripBeginCoordinates = rows[tripBeginIndex][1] + ',' + rows[tripBeginIndex][2];
            inTrip = true;
            tripDistance = 0;
            coords = [];
        }

        if (inTrip == true && isStop(rows, i, true)) {
            //push the stop point
            tripEndIndex = i;
            coords = pointsToMap(rows, tripBeginIndex, tripEndIndex);
            tripDistance = sumDistance(rows, tripBeginIndex, tripEndIndex);
            var tripTime = Math.abs(rows[i][0] - rows[tripBeginIndex][0]);
            const coordVar = [...coords];
            const cBeginDate = tripBeginDate;
            const cEndDate = rows[tripEndIndex][0];

            if (tripDistance > minTripDance && tripTime >= minTripDuration) {
                let row = table.insertRow(0);
                let startDate = row.insertCell(0);
                startDate.innerHTML = readableDate(cBeginDate);

                let endDate = row.insertCell(1);
                endDate.innerHTML = readableDate(cEndDate);

                let startPos = row.insertCell(2);
                startPos.innerHTML = tripBeginCoordinates;
                let stopPos = row.insertCell(3);
                stopPos.innerHTML = rows[tripEndIndex][1] + ',' + rows[tripEndIndex][2];
                let distance = row.insertCell(4);
                distance.innerHTML = Math.round(100 * tripDistance) / 100 + ' km';

                const distanceText = '  distance: ' + distance.innerHTML;
                const slicedHistory = rows.slice(tripBeginIndex, tripEndIndex);

                row.onclick = function() {
                    if (!unfilteredHistory.length) unfilteredHistory = historyItems;
                    tripActive = true;

                    //reduce events/logging/results/stats
                    filterEvents(cBeginDate, cEndDate);
                    filterLogging(cBeginDate, cEndDate);
                    filterCommandResults(cBeginDate, cEndDate);
                    filterStats(cBeginDate, cEndDate);
                    historyItems = slicedHistory;

                    travelFeature.getGeometry().setCoordinates(coordVar);
                    travelFeature.setStyle(travelLayerStyle(travelFeature));
                    distanceDiv.innerHTML = distanceText;
                    noUpdateCurrentPosition=true;
                    map.getView().animate({
                        center: coordVar[0],
                        duration: 1000
                    });
                };
            }

            inTrip = false;
        }

    }
}

function exitTrip() {
    tripActive = false;
    filterEvents(getSelectedBeginDate(), getSelectedEndDate());
    filterLogging(getSelectedBeginDate(), getSelectedEndDate());
    filterCommandResults(getSelectedBeginDate(), getSelectedEndDate());
    filterStats(getSelectedBeginDate(), getSelectedEndDate());

    if (unfilteredHistory.length) {
        historyItems = unfilteredHistory;
        unfilteredHistory = [];
    }
    refreshHistory();
}
