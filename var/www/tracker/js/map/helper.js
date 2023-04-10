/**
 * helper functions
 */
function tounix(date) {
    if (date == null) {
        return 0;
    }

    return parseInt((date.getTime() / 1000).toFixed(0)) + (new Date(0, 1).getTimezoneOffset() * 60);
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

function utcTime(timeString) {
    let [hour, minute] = timeString.split(':');
    let tzOffset = (new Date()).getTimezoneOffset();
    let dt = new Date();
    dt.setHours(hour);
    dt.setMinutes(minute);
    dt.setSeconds(0);
    dt.setTime(dt.getTime() + (tzOffset * 60000));
    let totalMinutes = (parseInt(hour, 10) * 60) + parseInt(minute, 10) + tzOffset;
    let ret = timePad(dt.getHours()) + ':' + timePad(dt.getMinutes());
    return ret;
}

function localTime(timeString) {
    let [hour, minute] = timeString.split(':');
    let tzOffset = (new Date()).getTimezoneOffset();
    let dt = new Date();
    dt.setHours(hour);
    dt.setMinutes(minute);
    dt.setSeconds(0);
    dt.setTime(dt.getTime() - (tzOffset * 60000));

    return timePad(dt.getHours()) + ':' + timePad(dt.getMinutes());
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

function readableDate(dt) {
    return dt.toLocaleString();
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