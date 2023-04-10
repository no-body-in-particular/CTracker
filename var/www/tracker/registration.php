<?php
require 'lib.php';
require 'database.php';

session_start();

$message = '';
$ret = false;

if (isset($_GET['add']) && 'POST' === $_SERVER['REQUEST_METHOD']) {
    // Checking that the posted phrase match the phrase stored in the session
    if (isset($_SESSION['phrase']) && compareCaptcha($_SESSION['phrase'], $_POST['phrase'])) {
        if (validateEmail($_POST['email']) && !findUserByEmail($_POST['email'])) {
            if (findUser($_POST['username'])) {
                $message = 'User '.$_POST['username'].' already exists.';
            } else {
             if(validatePassword($_POST["pwd"])){
                $ret = updateUser($_GET['add'], $_POST['username'], $_POST['name'], $_POST['email'], $_POST['pwd']);
              }else{
                $message = "Invalid password. Must be between 8 and 32 characters containing at least 1 uppercase, 1 lowercase letter and 1 number.";
              }
            }
        } else {
            $message = 'Invalid email address - or an user with this address already exists.';
        }
    } else {
        $message = 'Captcha is not valid!';
    }
    // The phrase can't be used twice
    unset($_SESSION['phrase']);
}

if ((!$ret || null === $ret) && (null === $message || '' === $message)) {
    $message = 'Registration failed';
} else {
    if ((null === $message || '' === $message)) {
        $message = 'User account was registred!';
    }
}

?>

<!DOCTYPE html>
<html lang="en">
<head>
  <title>Registration</title>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
     <link rel="stylesheet" href="style/index.css" type="text/css">
     <script src="jquery/jquery-3.2.1.min.js"></script>
     <script src="js/store.js"></script>
</head>
<body>

<div class="login-page">
      <div class="form-inline">
      <form role="form" method="post" action="registration.php?add=<?php echo uniqid(); ?>">
      <input type="text" name="name" class="input"  placeholder="Name" />
      <input type="text" name="username" class="input" placeholder="Username" />
      <input type="email" name="email" class="input" placeholder="Email" />
      <input type="password" name="pwd" class="input"  placeholder="Password" /><br>
      <img src="captcha.php" /><br>
      <input type="text" name="phrase" class="input" placeholder="Captcha" /><br>
      <input type="submit" value ="Register" />
      <p class="message"><a><?php echo $message; ?></a></p>
  </form>
  </div>
</div>

</body>
</html>
