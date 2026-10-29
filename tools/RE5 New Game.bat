@echo off
rem Backs up the RE5 save, then resets it to an untouched new game with Professional unlocked.
python "%~dp0re5_newgame.py" %*
pause
