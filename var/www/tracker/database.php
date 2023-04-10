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

?>