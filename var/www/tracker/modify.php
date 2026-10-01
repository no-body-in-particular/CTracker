<?php

require_once 'lib.php';
require_once 'database.php';

start_session();
validateSession();

if (isReadonly()) {
    exit();
}

if (!validateEmail($_POST['email'])) {
    exit('Invalid email address');
}

if (!validateName($_POST['name'])) {
    exit('Invalid name');
}
if (!validateUsername($_POST['username'])) {
    exit('Invalid username');
}

$loggedinUser=findUser($_SESSION['login']);

if ($_SESSION['login'] !== $_POST['username'] && findUser($_POST['username'])) {
    exit('New username already exists');
}

if ($loggedinUser[3] != $_POST['email'] && findUserByEmail($_POST['email'])) {
    exit('An user with this email already exists.');
}

$pwd = isset($_POST['pwd']) ? $_POST['pwd'] : '';

// the form no longer arrives pre-filled with the stored hash, so an empty field means
// "leave my password alone" rather than "set it to the hash of an empty string", which is
// what this did before. updateUser() stores a value longer than 32 characters unchanged,
// and the stored value is a 128 character whirlpool hash, so handing it straight back
// leaves the password exactly as it was.
if ('' === $pwd) {
    $pwd = $loggedinUser[4];
} elseif (!validatePassword($pwd)) {
    // modify.php never checked this, so the profile form could set a password that the
    // registration form would have rejected
    exit('Invalid password. Must be 8 to 72 characters with at least 1 uppercase letter, 1 lowercase letter and 1 number. Spaces are allowed inside it, but not at either end.');
}

$ret = updateUser($_SESSION['uid'], $_POST['username'], $_POST['name'], $_POST['email'], $pwd);

if ($ret) {
    $message = 'Success';
} else {
    $message = 'Update failed';
}

?>