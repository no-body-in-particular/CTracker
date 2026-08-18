<?php

include 'database.php';

ini_set('display_errors', 1);
ini_set('display_startup_errors', 1);
error_reporting(E_ALL);


$message = '<a href="registration.php">register here</a><br><br><a href="recover.php">forgot password?</a>';
session_start();
session_unset();

if (isset($_GET['login'])) {
    $blocked = loginBlockedSeconds();

    if ($blocked > 0) {
        // the same message either way, so this cannot be used to tell a real username from
        // an invented one
        $message = '<a>Too many failed login attempts. Please try again in '
            .ceil($blocked / 60).' minute(s).</a>';
    } elseif (validateUser($_POST['username'], hash('whirlpool', $_POST['password']))) {
        clearLoginFailures();
        header('Location: tracker.php');
    } else {
        recordLoginFailure();
        $message = '<a>Wrong Username or Password</a>';
    }
}
?>
<!DOCTYPE html>
<html>
   <head>
      <link rel="stylesheet" href="style/index.css" type="text/css">
      <script src="jquery/jquery-3.7.1.js"></script>
      <script src="js/store.js"></script>
   </head>
   <body>
      <div class="login-page">
         <div class="form">
            <form method="post" class="login-form" action="<?php echo htmlspecialchars($_SERVER['PHP_SELF'], ENT_QUOTES, 'UTF-8'); ?>?login=true" >
                <input style="width:100%" type="text" placeholder="username" class="input" name="username" id="username" autocomplete="username"/>
                <input style="width:100%" type="password" placeholder="password" class="input" name="password" id="password"/>
               <button style="width:100%" class="button">login</button>
                <p class="message"><?php echo $message; ?></p>
            </form>
         </div>
      </div>
   </body>
</html>
