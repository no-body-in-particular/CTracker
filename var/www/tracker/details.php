<?php

//ini_set('display_startup_errors', 1);
//error_reporting(E_ALL);

require 'lib.php';
require 'database.php';

session_start();

validateSession();
if (isReadOnly()) {
    exit();
}

$user = findUser($_SESSION['login']);

// $user[4] is the password hash, and validateUser() compares that hash directly - so it
// is the credential, not a derivative of it. it used to be sent here to populate the
// account form, which then posted it back in a url. name, username and mail only.
echo $user[1].','.$user[2].','.$user[3];
?>