/**
 * helper functions
 */
function tounix(date) {
    if (date == null) {
        return 0;
    }

    return Math.ceil((date.getTime()/1000));
}

function timePad(num) {
    var s = "0" + num;
    return s.substr(s.length - 2);
}

function hourPart(min) {
    var h = (Math.floor(min / 60) % 24);
    return (h >= 24 ? (h - 24) : (h <= 0 ? h : 24 + h));
}

function minutePart(min) {
    var m = Math.floor(min % 60);
    return m;
}

/*
 * Which clock this device's times are meant in.
 *
 * Everything here used to convert against the browser's own timezone, which is only right
 * when the person looking happens to live where the device does. Two people in different
 * countries watching the same bracelet read different times off the same row, and a fence
 * one of them wrote landed an hour or two from where the other one thought they had put
 * it. One zone per device - the wearer's - settles it for both.
 *
 * Fetched once at startup from timezone.php, which answers the server's own zone until
 * somebody chooses one. Kept as an IANA name so daylight saving comes from the zone
 * database rather than from anyone remembering to adjust it.
 */
var deviceTimezone = null;

function deviceZone() {
    //before the fetch lands, the browser's own zone is the only thing available - the same
    //answer as before this existed, so nothing is worse than it was
    return deviceTimezone || Intl.DateTimeFormat().resolvedOptions().timeZone;
}

/*
 * How far the device's zone is from UTC at a given moment, in the sign getTimezoneOffset()
 * uses: minutes to ADD to a local time to get UTC, so positive west of Greenwich.
 *
 * Read out of Intl rather than computed, so the answer already accounts for whether summer
 * time was in force at that particular moment - which is the part nobody wants to do by
 * hand twice a year.
 */
function zoneOffsetMinutes(when) {
    when = when || new Date();

    try {
        var parts = {};
        new Intl.DateTimeFormat('en-US', {
            timeZone: deviceZone(), hour12: false,
            year: 'numeric', month: '2-digit', day: '2-digit',
            hour: '2-digit', minute: '2-digit', second: '2-digit'
        }).formatToParts(when).forEach(function (p) { parts[p.type] = p.value; });

        //the same instant written as the zone's wall clock, then read back as though that
        //wall clock were UTC: the gap between the two is the offset
        var asIfUtc = Date.UTC(+parts.year, +parts.month - 1, +parts.day,
                               (+parts.hour) % 24, +parts.minute, +parts.second);

        return -Math.round((asIfUtc - when.getTime()) / 60000);

    } catch (e) {
        //an unknown zone name would otherwise take the whole page down with it
        return when.getTimezoneOffset();
    }
}

//"HH:MM" plus a number of minutes, wrapped into a day. Returns [dayShift, "HH:MM"], where
//dayShift is -1 when the result fell back into yesterday and +1 when it ran into tomorrow.
function shiftClock(timeString, minutes) {
    var bits = timeString.split(':');
    var total = (parseInt(bits[0], 10) * 60) + parseInt(bits[1], 10) + minutes;
    var dayShift = Math.floor(total / 1440);

    total = ((total % 1440) + 1440) % 1440;

    return [dayShift, timePad(Math.floor(total / 60)) + ':' + timePad(total % 60)];
}

/*
 * A time typed in the device's zone, as UTC. The caller adds the day shift to the weekday
 * the fence was given, because 00:30 on Monday in Berlin is 22:30 on Sunday on the wire.
 *
 * The day handling here used to read
 *     offset = dayLT ? -1 : 0;  offset = dayGT ? 1 : dayLT;
 * where the second line overwrote the first and left a bare boolean, so a time that
 * crossed backwards over midnight came out as +1 exactly like one that crossed forwards.
 * Both directions moved the fence to the same wrong day.
 */
function utcTime(timeString) {
    return shiftClock(timeString, zoneOffsetMinutes());
}

//and back again, for showing a stored fence time
function localTime(timeString) {
    return shiftClock(timeString, -zoneOffsetMinutes());
}

function deg2rad(deg) {
    return deg * (Math.PI / 180)
}

function haversineDistance(lat1, lon1, lat2, lon2) {
    var R = 6371; // Radius of the earth in km
    var dLat = deg2rad(lat2 - lat1); // deg2rad below
    var dLon = deg2rad(lon2 - lon1);
    var a =
        Math.sin(dLat / 2) * Math.sin(dLat / 2) +
        Math.cos(deg2rad(lat1)) * Math.cos(deg2rad(lat2)) *
        Math.sin(dLon / 2) * Math.sin(dLon / 2);
    var c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
    var d = R * c; // Distance in km
    return d;
}


function distance(x, y, a, b) {
    return Math.sqrt((y - b) * (y - b) + (x - a) * (x - a));
}

/*
 * Every time on the page, in the device's zone with the zone named.
 *
 * This was toLocaleString(), which renders in whatever zone the browser is in and says so
 * nowhere - so the guard abroad and the wearer at home read the same event as two different
 * times, each believing their own. Naming the zone costs a few characters and removes the
 * question entirely.
 */
function readableDate(dt) {
    try {
        return dt.toLocaleString(undefined, {
            timeZone: deviceZone(),
            year: 'numeric', month: '2-digit', day: '2-digit',
            hour: '2-digit', minute: '2-digit', second: '2-digit',
            timeZoneName: 'short'
        });

    } catch (e) {
        return dt.toLocaleString();
    }
}

function sumDistance(rows, beginIndex, endIndex) {
    var dist = 0.0;
    for (var i = beginIndex;
        (i < (rows.length - 1)) && (i < endIndex); i++) {
        dist += haversineDistance(rows[i][1], rows[i][2], rows[i + 1][1], rows[i + 1][2]);
    }
    return dist;
}

function distanceToMedian(rows, beginIndex, endIndex) {
    var dist = 0.0;
    var latSum = 0;
    var longSum = 0;
    var loopCount = 0;

    for (var i = beginIndex;
        (i < (rows.length - 1)) && (i < endIndex); i++) {
        dist += haversineDistance(rows[i][1], rows[i][2], rows[i + 1][1], rows[i + 1][2]);
    }
    return dist;
}

function combineDT(nameD, nameT) {
    const dtCompName = '#' + nameD;
    const tCompName = '#' + nameT;
    var dInput;
    var tInput;

    if ($(dtCompName).prop('type') != 'date') {
        dInput = $(dtCompName).val();
        tInput = $(tCompName).val();
    } else {
        dInput = document.getElementById(nameD).value;
        tInput = document.getElementById(nameT).value;
    }

    if (dInput && tInput) {
        return new Date(dInput + 'T' + tInput);
    }

    if (dInput) return new Date(dInput);

    return null;
}

function isRowVisible(el) {
    var rect = el.getBoundingClientRect();
    var top = rect.top;
    var height = rect.height;

    el = el.parentNode;

    // Check if bottom of the element is off the parent element
    if (rect.bottom < 0) return false;
    // Check its within the document viewport
    if (top > document.documentElement.clientHeight) return false;
    do {
        rect = el.getBoundingClientRect();
        if (top <= rect.bottom === false) return false;
        // Check if the element is out of view due to a container scrolling
        if ((top + height) <= rect.top) return false;
        el = el.parentNode;
    } while (el != document.body)
    return true;
};


function forEachRow(page, minCols, func) {
    var lines = page.split(/\r?\n/);
    var ret = lines.map(line => line.split(',')).filter(cols => cols.length > minCols).map(cols => func(cols));
    return ret;
}

function fallbackCopyTextToClipboard(text) {
    var textArea = document.createElement("textarea");
    textArea.value = text;

    // Avoid scrolling to bottom
    textArea.style.top = "0";
    textArea.style.left = "0";
    textArea.style.position = "fixed";

    document.body.appendChild(textArea);
    textArea.focus();
    textArea.select();

    try {
        var successful = document.execCommand('copy');
        var msg = successful ? 'successful' : 'unsuccessful';
    } catch (err) {}

    document.body.removeChild(textArea);
}

function copyTextToClipboard(text) {
    if (!navigator.clipboard) {
        fallbackCopyTextToClipboard(text);
        return;
    }
    navigator.clipboard.writeText(text).then(function() {}, function(err) {
        console.error('Async: Could not copy text: ', err);
    });
}

function viewOnlyParameter() {
    const urlParams = new URLSearchParams(window.location.search);
    if (urlParams.get('viewonly')) {
        return '&viewonly=' + urlParams.get('viewonly');
    }
    return '';
}
/*
 * Every table on this page is built by concatenating values into innerHTML, and some of those
 * values are whatever the device sent - event descriptions, log lines, command responses - or
 * whatever the user typed into a device name. None of it was escaped, so a device name of
 * <img src=x onerror=...> ran as soon as the list was drawn.
 *
 * Quotes are escaped as well as the tag characters, so a value cannot break out of an attribute
 * it is placed in either.
 */
function escapeHtml(value) {
    if (value === null || value === undefined) {
        return '';
    }

    return String(value)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

//for a value that ends up inside a javascript string inside an attribute, where the html
//parser hands the quote back before the script sees it
function escapeNumber(value) {
    var n = parseFloat(value);
    return isFinite(n) ? n : 0;
}
