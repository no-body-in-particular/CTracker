<?php

include 'config.php';

function getDevice($view_alias)
{
    session_start();

    $db = new SQLite3(DATABASE);

    $id_db = uniqid();
    $filtered_alias = SQLite3::escapeString($view_alias);

    $sql = 'SELECT * FROM DEVICES WHERE VIEW_ALIAS=:alias;';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':alias', $filtered_alias, SQLITE3_TEXT);
    $ret = $stmt->execute();
    $result = $ret->fetchArray();
    $db->close();

    return $result;
}

function validateViewOnly()
{
    if (isReadonly()) {
        $dev = getDevice($_GET['viewonly']);
        if (null === $dev) {
            $dev = getDevice($_SESSION['viewonly']);
        }

        if (null === $dev) {
            exit('<html<body>Please login first <meta http-equiv="Refresh" content="3; url=index.php" /></body></html>');
        }
        if (null !== $_GET['imei'] && ($dev[1] !== $_GET['imei'])) {
            exit('<html<body>Please login first <meta http-equiv="Refresh" content="3; url=index.php" /></body></html>');
        }

        $_SESSION['viewonly'] = $_GET['viewonly'];

        return true;
    }

    return false;
}

function readDevices(): void
{
    session_start();
    if (isReadonly()) {
        validateViewOnly();

        $dev = getDevice($_SESSION['viewonly']);
        if (null === $dev) {
            return;
        }
        echo $dev['ID'].',';
        echo $dev['IMEI'].',';
        echo $dev['NAME'].',';
        echo $dev['VIEW_ALIAS'];
        echo "\n";

        return;
    }
    $filtered_username = SQLite3::escapeString($username);

    $db = new SQLite3(DATABASE);
    $sql = 'SELECT * from DEVICES where USER_ID=:uid;';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':uid', $_SESSION['uid'], SQLITE3_TEXT);
    $ret = $stmt->execute();

    while ($result = $ret->fetchArray()) {
        echo $result['ID'].',';
        echo $result['IMEI'].',';
        echo $result['NAME'].',';
        echo $result['VIEW_ALIAS'];
        echo "\n";
    }

    $db->close();
}

function deviceClaimed($imei)
{
    session_start();

    $db = new SQLite3(DATABASE);

    $id_db = uniqid();
    $filtered_imei = SQLite3::escapeString($imei);
    $filtered_uid = $_SESSION['uid'];

    $sql = 'SELECT * FROM DEVICES WHERE IMEI=:imei;';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':imei', $filtered_imei, SQLITE3_TEXT);
    $stmt->bindValue(':uid', $filtered_uid, SQLITE3_TEXT);
    $ret = $stmt->execute();
    $result = $ret->fetchArray();
    $db->close();

    return $result;
}

function addDevice($imei, $name)
{
    session_start();

    $db = new SQLite3(DATABASE);

    $id_db = uniqid();
    $filtered_imei = SQLite3::escapeString($imei);
    $filtered_name = SQLite3::escapeString($name);
    $filtered_uid = $_SESSION['uid'];

    $sql = 'INSERT OR REPLACE INTO DEVICES (ID,IMEI,NAME,VIEW_ALIAS,USER_ID) VALUES (:id, :imei, :name , :view_alias, :uid);';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':id', uniqid(), SQLITE3_TEXT);
    $stmt->bindValue(':imei', $filtered_imei, SQLITE3_TEXT);
    $stmt->bindValue(':name', $filtered_name, SQLITE3_TEXT);
    $stmt->bindValue(':view_alias', uniqid(), SQLITE3_TEXT);
    $stmt->bindValue(':uid', $filtered_uid, SQLITE3_TEXT);

    $ret = $stmt->execute();

    $db->close();

    return $ret;
}

function removeDevice($imei)
{
    session_start();

    $db = new SQLite3(DATABASE);

    $id_db = uniqid();
    $filtered_imei = SQLite3::escapeString($imei);
    $filtered_uid = $_SESSION['uid'];

    $sql = 'DELETE FROM DEVICES WHERE IMEI=:imei AND USER_ID=:uid;';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':imei', $filtered_imei, SQLITE3_TEXT);
    $stmt->bindValue(':uid', $filtered_uid, SQLITE3_TEXT);

    $ret = $stmt->execute();

    $db->close();

    return $ret;
}

function isMyDevice($imei)
{
    session_start();

    $db = new SQLite3(DATABASE);

    $id_db = uniqid();
    $filtered_imei = SQLite3::escapeString($imei);
    $filtered_uid = $_SESSION['uid'];

    $sql = 'SELECT * FROM DEVICES WHERE IMEI=:imei AND USER_ID=:uid;';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':imei', $filtered_imei, SQLITE3_TEXT);
    $stmt->bindValue(':uid', $filtered_uid, SQLITE3_TEXT);
    $ret = $stmt->execute();
    $result = $ret->fetchArray();
    $db->close();

    return $result;
}

function validateIMEI($imei)
{
    session_start();

    if (validateViewOnly()) {
        return true;
    }
    if (isMyDevice($imei)) {
        return true;
    }

    exit('');
}

function findUser($username)
{
    $filtered_username = SQLite3::escapeString($username);

    $db = new SQLite3(DATABASE);
    $sql = 'SELECT * from USERS where USERNAME=:username;';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':username', $filtered_username, SQLITE3_TEXT);
    $ret = $stmt->execute();
    $result = $ret->fetchArray();
    $db->close();

    return $result;
}

function findUserByEmail($email){
    $filtered_email = SQLite3::escapeString($email);

    $db = new SQLite3(DATABASE);

    $sql = 'SELECT * from USERS where MAIL=:email ;';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':email', $filtered_email, SQLITE3_TEXT);
    $ret = $stmt->execute();
    $result = $ret->fetchArray();
    $db->close();

    return $result;
}

function getResetKey($email)
{
    $random_id=base64_encode(random_bytes(14));
    $filtered_email = SQLite3::escapeString($email);

    $result=findUserByEmail($email);

    if(!$result){
        return null;
    }

    $db = new SQLite3(DATABASE);
    $sql = 'UPDATE USERS SET RESETKEY = :key WHERE MAIL = :email';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':key', $random_id, SQLITE3_TEXT);
    $stmt->bindValue(':email', $filtered_email, SQLITE3_TEXT);
    $ret = $stmt->execute();
    $db->close();

    return $random_id;
}


function getResetUser($key)
{
    $filtered_key = SQLite3::escapeString($key);

    if($filtered_key  == '' || $filtered_key == null){
        return null;
    }

    $db = new SQLite3(DATABASE);

    $sql = 'SELECT * from USERS where RESETKEY=:key;';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':key', $filtered_key, SQLITE3_TEXT);
    $ret = $stmt->execute();
    $result = $ret->fetchArray();
    $db->close();
    return $result;
}

function updatePassword($id,$pwd){
    $filtered_id = SQLite3::escapeString($id);
    $filtered_pwd = hash('whirlpool', $pwd);

    $db = new SQLite3(DATABASE);
    $sql = 'UPDATE USERS SET PASSWORD = :pwd, RESETKEY = \'\' WHERE ID = :id ;';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':id', $filtered_id, SQLITE3_TEXT);
    $stmt->bindValue(':pwd', $filtered_pwd, SQLITE3_TEXT);
    $ret = $stmt->execute();

    if($ret==FALSE)
    {
        echo "Error in update ".$db->lastErrorMsg();
    }

    $db->close();



    return $ret;
}


function updateUser($id, $username, $name, $email, $pwd)
{
    $db = new SQLite3(DATABASE);

    $filtered_id = SQLite3::escapeString($id);
    $filtered_username = SQLite3::escapeString($username);
    $filtered_name = SQLite3::escapeString($name);
    $filtered_email = SQLite3::escapeString($email);
    $filtered_password = strlen($pwd) > 32 ? SQLite3::escapeString($pwd) : hash('whirlpool', $pwd);

    $sql = 'INSERT OR REPLACE INTO USERS (ID,NAME,USERNAME,MAIL,PASSWORD,RESETKEY) VALUES (:id, :name, :username , :email, :pwd, \'\');';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':id', $filtered_id, SQLITE3_TEXT);
    $stmt->bindValue(':name', $filtered_name, SQLITE3_TEXT);
    $stmt->bindValue(':username', $filtered_username, SQLITE3_TEXT);
    $stmt->bindValue(':email', $filtered_email, SQLITE3_TEXT);
    $stmt->bindValue(':pwd', $filtered_password, SQLITE3_TEXT);

    $ret = $stmt->execute();

    $db->close();

    return $ret;
}

function validateUser($username, $password)
{
    $filtered_username = SQLite3::escapeString($username);
    $filtered_password = SQLite3::escapeString($password);

    $valid = false;
    $db = new SQLite3(DATABASE);

    $sql = 'SELECT * from USERS where USERNAME=:username AND PASSWORD=:pwd;';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':username', $filtered_username, SQLITE3_TEXT);
    $stmt->bindValue(':pwd', $filtered_password, SQLITE3_TEXT);
    $ret = $stmt->execute();
    $result = $ret->fetchArray();

    if ($result) {
        session_start();
        $_SESSION['login'] = $filtered_username;
        $_SESSION['password'] = $filtered_password;
        $_SESSION['uid'] = $result[0];

        $valid = true;
    }

    $db->close();

    return $valid;
}

function isReadonly()
{
    return null !== $_GET['viewonly'] || null !== $_SESSION['viewonly'];
}

function validateSession()
{
    session_start();

    $username = $_SESSION['login'];
    $password = $_SESSION['password'];

    if (null === $username || null === $password) {
        $username = $_GET['username'];
        $password = $_GET['password'];
    }

    if (validateUser($username, $password)) {
        $_SESSION['viewonly'] = null;

        return true;
    }
    if (!isReadonly()) {
        exit('<html<body>Please login first <meta http-equiv="Refresh" content="3; url=index.php" /></body></html>');
    }
}

/*
 * Login throttling.
 *
 * Keyed on the client address rather than the username on purpose: throttling per account
 * would let anyone lock a legitimate user out of their own tracker just by failing logins
 * against their name. The table is created on demand, so there is no migration step.
 *
 * Only the login form calls these. validateSession() runs validateUser() on every
 * authenticated request, and counting those would throttle normal use.
 */
define('LOGIN_MAX_FAILURES', 10);   // failures allowed inside the window
define('LOGIN_WINDOW', 900);        // window, seconds
define('LOGIN_LOCKOUT', 900);       // how long a lockout lasts, seconds

function loginThrottleDb()
{
    $db = new SQLite3(DATABASE);
    $db->busyTimeout(5000);
    $db->exec('CREATE TABLE IF NOT EXISTS LOGIN_ATTEMPTS ('
        .'IP TEXT PRIMARY KEY, FAILURES INTEGER NOT NULL DEFAULT 0, '
        .'FIRST_FAILURE INTEGER NOT NULL DEFAULT 0, BLOCKED_UNTIL INTEGER NOT NULL DEFAULT 0);');

    return $db;
}

function loginClientIp()
{
    // deliberately not X-Forwarded-For: that is client supplied, and trusting it would let
    // an attacker pick a fresh identity per request and skip the limit entirely
    return isset($_SERVER['REMOTE_ADDR']) ? $_SERVER['REMOTE_ADDR'] : 'unknown';
}

// Returns seconds remaining on a lockout, or 0 when the caller may attempt a login.
function loginBlockedSeconds()
{
    $db = loginThrottleDb();
    $now = time();

    // opportunistic tidy up so the table cannot grow without bound
    $db->exec('DELETE FROM LOGIN_ATTEMPTS WHERE BLOCKED_UNTIL < '.($now - LOGIN_WINDOW)
        .' AND FIRST_FAILURE < '.($now - LOGIN_WINDOW));

    $stmt = $db->prepare('SELECT BLOCKED_UNTIL FROM LOGIN_ATTEMPTS WHERE IP=:ip;');
    $stmt->bindValue(':ip', loginClientIp(), SQLITE3_TEXT);
    $row = $stmt->execute()->fetchArray();
    $db->close();

    if (!$row) {
        return 0;
    }

    $remaining = ((int) $row['BLOCKED_UNTIL']) - $now;

    return $remaining > 0 ? $remaining : 0;
}

function recordLoginFailure(): void
{
    $db = loginThrottleDb();
    $now = time();
    $ip = loginClientIp();

    $stmt = $db->prepare('SELECT FAILURES, FIRST_FAILURE FROM LOGIN_ATTEMPTS WHERE IP=:ip;');
    $stmt->bindValue(':ip', $ip, SQLITE3_TEXT);
    $row = $stmt->execute()->fetchArray();

    $failures = 1;
    $first = $now;

    if ($row) {
        // a window that has already elapsed starts again rather than accumulating forever
        if (((int) $row['FIRST_FAILURE']) > ($now - LOGIN_WINDOW)) {
            $failures = ((int) $row['FAILURES']) + 1;
            $first = (int) $row['FIRST_FAILURE'];
        }
    }

    $blocked_until = $failures >= LOGIN_MAX_FAILURES ? $now + LOGIN_LOCKOUT : 0;

    $stmt = $db->prepare('INSERT OR REPLACE INTO LOGIN_ATTEMPTS (IP,FAILURES,FIRST_FAILURE,BLOCKED_UNTIL) '
        .'VALUES (:ip,:failures,:first,:blocked);');
    $stmt->bindValue(':ip', $ip, SQLITE3_TEXT);
    $stmt->bindValue(':failures', $failures, SQLITE3_INTEGER);
    $stmt->bindValue(':first', $first, SQLITE3_INTEGER);
    $stmt->bindValue(':blocked', $blocked_until, SQLITE3_INTEGER);
    $stmt->execute();
    $db->close();
}

function clearLoginFailures(): void
{
    $db = loginThrottleDb();
    $stmt = $db->prepare('DELETE FROM LOGIN_ATTEMPTS WHERE IP=:ip;');
    $stmt->bindValue(':ip', loginClientIp(), SQLITE3_TEXT);
    $stmt->execute();
    $db->close();
}

?>
