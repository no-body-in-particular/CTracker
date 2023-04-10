function viewDevice(no) {
    imei = String(no).padStart(16, '0');
    newImei = new Date().getTime();
    setTimeout(updateCurrentPosition, 2);
    setTimeout(searchdateChange, 2);
}

function viewOnlyLink(param) {
    var url = location.protocol + '//' + location.host + location.pathname + "?viewonly=" + param;
    copyTextToClipboard(url);
    window.open(url);
}

function computeDeviceRow(cols) {
    return '<tr ondblclick=viewDevice("' + cols[0] + '")><td>' + cols[0] + '</td><td>' + cols[1] + '</td><td><button class="button" onclick="viewDevice(\'' + cols[0] + '\');">View</button><button class="button" onclick=\'removeDevice("' + cols[0] + '")\'>Remove</button><button class="button" onclick=viewOnlyLink("' + cols[2] + '")>View-only link</button></td></tr>';
}


function fetchUser() {
    $.ajax({
        url: "details.php?action=read" + viewOnlyParameter(),
        success: function(result) {
            result = result.replace("\n", "");
            var parsed = forEachRow(result, 3, cols => [cols[0], cols[1], cols[2], cols[3]]);
            if (parsed.length) {
                const usernameField = document.getElementById("username");
                const nameField = document.getElementById("name_for_account");
                const emailField = document.getElementById("email");
                const passwordField = document.getElementById("password");

                nameField.value = parsed[0][0];
                usernameField.value = parsed[0][1];
                emailField.value = parsed[0][2];
                passwordField.value = parsed[0][3];
            }
        }
    });
}

function updateUser() {
    $.ajax({
        url: "modify.php?username=" + document.getElementById("username").value +
            "&name=" + document.getElementById("name_for_account").value +
            "&email=" + document.getElementById("email").value +
            "&pwd=" + document.getElementById("password").value,
        success: function(result) {
            alert(result);
            fetchUser();

        }
    });
}


function fetchDevices() {
    $.ajax({
        url: "devicelist.php?action=read" + viewOnlyParameter(),
        success: function(result) {
            var parsed = forEachRow(result, 3, cols => [cols[1], cols[2], cols[3]]);
            if ((imei == null || imei == '') && parsed.length > 0) {
                viewDevice(parsed[0][0]);
            }
            const tableBody = document.getElementById("deviceBody");
            tableBody.innerHTML = parsed.map(rv => computeDeviceRow(rv)).join('');
        }
    });
}

function saveDevice() {
    $.ajax({
        url: "devicelist.php?imei=" + String(document.getElementById("imei").value).padStart(16, '0') + "&action=write&name=" + document.getElementById("name").value,
        success: function(result) {
            alert(result);
            refreshDevices();
        }
    });
}

function removeDevice(imei) {
    $.ajax({
        url: "devicelist.php?action=remove&imei=" + imei,
        success: function(result) {
            refreshDevices();
        }
    });
}

function refreshDevices() {
    const table = document.getElementById("deviceTable");
    var firstRowTimestamp;

    for (i = 1; i < table.rows.length; i++) {
        let row = table.rows[i];
        if (isRowVisible(row)) {
            firstRowTimestamp = row.cells[0].innerHTML;
            break;
        }
    }

    fetchDevices();

    for (var i = 0; i < table.rows.length; i++) {
        let row = table.rows[i];
        if (row.cells[0].innerHTML == firstRowTimestamp) {
            row.scrollIntoView();
            break;
        }
    }
}


refreshDevices();
fetchUser();