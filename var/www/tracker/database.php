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

/*
 * Password storage migration.
 *
 * Passwords were stored as an unsalted whirlpool digest, and validateUser() compared the
 * stored digest against one the caller computed - so the digest was the credential, and
 * anyone who read it could authenticate. They are now stored with password_hash(), which
 * is salted and deliberately slow.
 *
 * Both formats coexist. A stored value is recognised by its shape rather than by a flag,
 * which cannot drift out of step with the data: legacy is exactly 128 hex characters,
 * password_hash() output starts with $. On the next successful login with a legacy
 * password the plaintext is briefly in hand, so it is rehashed then and there. Nobody has
 * to reset anything and no account is locked out; the old digests drain away as people
 * log in.
 *
 * PASSWORD_UPDATED records when each hash was written, so the progress of the migration
 * is visible - see pendingLegacyPasswords() - and a cutoff can be applied later to any
 * account that has not logged in by then.
 */
function ensureUserSchema($db): void
{
    $cols = [];
    $res = $db->query('PRAGMA table_info(USERS);');

    while ($row = $res->fetchArray(SQLITE3_ASSOC)) {
        $cols[] = $row['name'];
    }

    // sqlite has no ADD COLUMN IF NOT EXISTS
    if (!in_array('PASSWORD_UPDATED', $cols, true)) {
        $db->exec('ALTER TABLE USERS ADD COLUMN PASSWORD_UPDATED INTEGER DEFAULT 0;');
    }
}

// A legacy whirlpool digest: 128 hex characters and nothing else.
function isLegacyHash($value)
{
    return is_string($value) && 1 === preg_match('/^[0-9a-f]{128}$/iD', $value);
}

// Anything password_hash() produced. Every crypt format it emits begins with $.
function isModernHash($value)
{
    return is_string($value) && 0 === strncmp($value, '$', 1) && strlen($value) >= 20;
}

// True when the value is already a stored hash rather than a plaintext password. Used to
// let a caller hand back the value it was given without it being hashed a second time.
function isStoredHash($value)
{
    return isLegacyHash($value) || isModernHash($value);
}

function storePasswordHash($id, $hash): void
{
    $db = new SQLite3(DATABASE);
    $db->busyTimeout(5000);
    ensureUserSchema($db);

    $stmt = $db->prepare('UPDATE USERS SET PASSWORD = :pwd, PASSWORD_UPDATED = :ts WHERE ID = :id;');
    $stmt->bindValue(':pwd', $hash, SQLITE3_TEXT);
    $stmt->bindValue(':ts', time(), SQLITE3_INTEGER);
    $stmt->bindValue(':id', $id, SQLITE3_TEXT);
    $stmt->execute();
    $db->close();
}

// How many accounts are still on the old format, for keeping an eye on the migration.
function pendingLegacyPasswords()
{
    $db = new SQLite3(DATABASE);
    $res = $db->query('SELECT PASSWORD FROM USERS;');
    $n = 0;

    while ($row = $res->fetchArray(SQLITE3_ASSOC)) {
        if (isLegacyHash($row['PASSWORD'])) {
            ++$n;
        }
    }

    $db->close();

    return $n;
}

/*
 * The login check. Takes the plaintext, because password_verify() needs it - the caller
 * used to hash first, which is why the digest ended up travelling around.
 */
function validateLogin($username, $password)
{
    $user = findUser($username);

    if (!$user) {
        // spend roughly the time a real verify would, so a missing account is not
        // obviously faster than a wrong password
        password_verify($password, '$2y$10$usesomesillystringforsalt.rMPTNPd7qF/6qvbmLxHXTnT8LP6');

        return false;
    }

    $stored = $user[4];
    $valid = false;

    if (isLegacyHash($stored)) {
        $valid = hash_equals($stored, hash('whirlpool', $password));

        if ($valid) {
            // the one moment the plaintext is available - upgrade it now
            $stored = password_hash($password, PASSWORD_DEFAULT);
            storePasswordHash($user[0], $stored);
        }
    } elseif (isModernHash($stored)) {
        $valid = password_verify($password, $stored);

        if ($valid && password_needs_rehash($stored, PASSWORD_DEFAULT)) {
            // picks up any future change to the default algorithm or cost
            $stored = password_hash($password, PASSWORD_DEFAULT);
            storePasswordHash($user[0], $stored);
        }
    }

    if (!$valid) {
        return false;
    }

    session_start();
    $_SESSION['login'] = $username;
    // the current hash, not the password. it identifies this session against the stored
    // value so that changing the password ends the session, but unlike the old whirlpool
    // digest it cannot be replayed as a login - that needs the plaintext.
    $_SESSION['pwhash'] = $stored;
    $_SESSION['uid'] = $user[0];

    return true;
}

function updatePassword($id,$pwd){
    $filtered_id = SQLite3::escapeString($id);
    $filtered_pwd = password_hash($pwd, PASSWORD_DEFAULT);

    $db = new SQLite3(DATABASE);
    ensureUserSchema($db);
    $sql = 'UPDATE USERS SET PASSWORD = :pwd, PASSWORD_UPDATED = :ts, RESETKEY = \'\' WHERE ID = :id ;';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':id', $filtered_id, SQLITE3_TEXT);
    $stmt->bindValue(':ts', time(), SQLITE3_INTEGER);
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
    // the old test was "longer than 32 characters means it is already hashed", which is
    // fragile - a 33 character password would have been stored as plaintext. check the
    // shape of the value instead. modify.php relies on this to hand back the stored hash
    // unchanged when the password field is left blank.
    $filtered_password = isStoredHash($pwd) ? $pwd : password_hash($pwd, PASSWORD_DEFAULT);

    ensureUserSchema($db);

    // INSERT OR REPLACE rewrites the whole row, so PASSWORD_UPDATED has to be listed here
    // or it would be reset to its default every time a profile is edited
    $sql = 'INSERT OR REPLACE INTO USERS (ID,NAME,USERNAME,MAIL,PASSWORD,RESETKEY,PASSWORD_UPDATED) VALUES (:id, :name, :username , :email, :pwd, \'\', :ts);';
    $stmt = $db->prepare($sql);
    $stmt->bindValue(':ts', time(), SQLITE3_INTEGER);
    $stmt->bindValue(':id', $filtered_id, SQLITE3_TEXT);
    $stmt->bindValue(':name', $filtered_name, SQLITE3_TEXT);
    $stmt->bindValue(':username', $filtered_username, SQLITE3_TEXT);
    $stmt->bindValue(':email', $filtered_email, SQLITE3_TEXT);
    $stmt->bindValue(':pwd', $filtered_password, SQLITE3_TEXT);

    $ret = $stmt->execute();

    $db->close();

    return $ret;
}

/*
 * validateUser() used to live here. It took a digest the caller had already computed and
 * looked for a row matching it, which is only possible with an unsalted hash and is what
 * made the stored value directly usable as a credential. Logging in goes through
 * validateLogin() with the plaintext; a session is checked against the stored hash in
 * validateSession().
 */

function isReadonly()
{
    return null !== $_GET['viewonly'] || null !== $_SESSION['viewonly'];
}

function validateSession()
{
    session_start();

    $username = $_SESSION['login'];
    $pwhash = $_SESSION['pwhash'] ?? null;

    // credentials used to fall back to $_GET['username'] and $_GET['password'] when the
    // session was empty. anything in a query string ends up in the webserver access log,
    // the browser history and any referrer header, so a link shared or pasted once leaked
    // the account permanently. read only sharing already has a proper mechanism - the
    // viewonly alias, which is a per device token that can be regenerated.
    //
    // dropping it also removes a collision: modify.php passes the *new* username and
    // password in $_GET['username'] and $_GET['pwd'] when updating a profile.
    if (null !== $username && null !== $pwhash) {
        $user = findUser($username);

        // comparing against the stored hash keeps the old behaviour that changing a
        // password ends every existing session, without the session holding anything that
        // could be replayed as a login
        if ($user && is_string($user[4]) && hash_equals($user[4], $pwhash)) {
            $_SESSION['viewonly'] = null;

            return true;
        }
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
 * Only the login form calls these. validateSession() runs on every authenticated
 * request, and counting those would throttle normal use.
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
