<?php

include_once 'database.php';
include_once 'lib.php';

start_session();

$message = '';

if ('POST' === $_SERVER['REQUEST_METHOD'] ) {
   // Checking that the posted phrase match the phrase stored in the session

   if (isset($_SESSION['phrase']) && compareCaptcha($_SESSION['phrase'], $_POST['phrase'])) {
      $key=getResetKey($_POST['email']);
      $message='Mail sent. Please check your mailbox for a password reset link.';
         if($key){
            // Wrap the sentence, never the URL: wordwrap() with cut=true will
            // break a long link mid-token, and a base64 key with enough
            // +, / or = in it pushes the URL past 70 characters (76 at worst).
            // That silently mailed an unclickable link about 1 time in 500.
            $link = "https://coredump.ws/tracker/reset.php?key=" . urlencode($key);
            $msg = wordwrap("You can reset your password with the following link:", 70, "\n", true)
                 . "\n" . $link;
            mail($_POST['email'],"GPS tracker password reset",$msg);
         }
      }else{
       //  $message= $_SESSION['phrase'] . '  ' . $_POST['phrase'];
         $message='Please enter the Captcha correctly.';
      }
}
?>
<html>
   <head>
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <link rel="stylesheet" href="style/index.css" type="text/css">
      <script src="jquery/jquery-3.2.1.min.js"></script>
      <script src="js/store.js"></script>
   </head>
   <body>
      <div class="login-page">
         <div class="form">
            <form method="post" class="login-form" action="<?php echo htmlspecialchars($_SERVER['PHP_SELF'], ENT_QUOTES, 'UTF-8'); ?>" >
                <input style="width:100%" type="text" placeholder="email address" class="input" name="email" id="email"/>
                <img src="captcha.php" /><br>
                <input type="text" name="phrase" class="input" placeholder="Captcha" /><br>
                <button style="width:100%" class="button">reset password</button>
                <p class="message"><?php echo $message; ?></p>
            </form>
         </div>
      </div>
   </body>
</html>
