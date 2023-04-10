<?php

include 'database.php';
include 'lib.php';

session_start();

$message = '';

if ('POST' === $_SERVER['REQUEST_METHOD'] ) {
   // Checking that the posted phrase match the phrase stored in the session

   if (isset($_SESSION['phrase']) && compareCaptcha($_SESSION['phrase'], $_POST['phrase'])) {
      $key=getResetKey($_POST['email']);
      $message='Mail sent. Please check your mailbox for a password reset link.';
         if($key){
            $msg="You can reset your password with the following link: https://coredump.ws/tracker/reset.php?key=" . urlencode($key);
            $msg = wordwrap($msg,70,"\n",true);
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
      <link rel="stylesheet" href="style/index.css" type="text/css">
      <script src="jquery/jquery-3.2.1.min.js"></script>
      <script src="js/store.js"></script>
   </head>
   <body>
      <div class="login-page">
         <div class="form">
            <form method="post" class="login-form" action="<?php echo $_SERVER['PHP_SELF']; ?>" >
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
