PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE USERS( 
   ID INT PRIMARY KEY     NOT NULL,
   NAME           TEXT    NOT NULL,
   USERNAME       TEXT     NOT NULL,
   MAIL           TEXT,
   PASSWORD       TEXT
, resetkey varchar);
CREATE TABLE DEVICES( 
   ID INT PRIMARY KEY          NOT NULL,
   IMEI           TEXT         NOT NULL,
   NAME           TEXT         NOT NULL,
   VIEW_ALIAS TEXT             NOT NULL,
   USER_ID INT                 NOT NULL
);
COMMIT;
