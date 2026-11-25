WORDLE for the Shanling Q2 (Q2 Pod)
===================================

Guess the hidden five-letter word in six tries. After each guess the tiles turn green (right
letter, right place), yellow (in the word, somewhere else) or grey (not in the word), and the
keyboard keeps score of every letter you've tried.

INSTALL
1. Copy the .wordle folder to the root of the card. On a Mac, press Cmd-Shift-. in Finder to see
   dot-folders.
2. Eject the card and open Library -> Games -> Wordle.

TWO GAMES
  Daily Puzzle   Today's word from The New York Times, the same one everyone gets, fetched over
                 Wi-Fi. A new puzzle comes at midnight (the Q2's own time zone). Each time it
                 fetches, the game also saves the next few days' words (hidden), so the Daily
                 still works on a day the Q2 has no Wi-Fi when you open it.
  Random Word    A word from the list built into the game. No Wi-Fi needed; play as many as you
                 like. Every word in the list comes up once before any comes again.

CONTROLS
  Wheel          move the highlight round the keyboard (or a menu)
  Centre         press the highlighted key; after the fifth letter the highlight jumps to Enter
  Touch          put a finger on a key: it lights up and shows bigger above your finger; slide
                 to the one you want and lift to type it
  Return         delete a letter; with none to delete, back to the menu
  Hold Return    leave the game (Q2 Pod's own)
The music keeps playing: Play/Pause and the side buttons stay the player's.

SETTINGS
  Hard Mode            hints must be used: a green letter stays where it is, a yellow one must
                       be in the guess (starts from your next game if one is under way)
  Dark Theme
  High Contrast Mode   orange and blue in place of green and yellow

IF THE DAILY CAN'T CONNECT
- Wi-Fi must be on and connected. With Smart Wi-Fi on, the radio may be asleep when the game
  starts: open Streaming or the Wi-Fi settings page once to wake it, then Try Again.
- If the Q2's clock was never set, the game asks the server what day it is.
- "Play a Random Word" is always there instead.

FILES
  q2-wordle      the game
  cacert.pem     certificate authorities, to check it's really the New York Times answering
  wordle.sav     your games, statistics and settings
  wordle.cache   the Daily's words saved ahead (hidden)
  wordle.log     what the game did, for when something goes wrong

Wordle for Q2 is an unofficial fan port and is not affiliated with The New York Times. Its code is
MIT-licensed; fonts Clear Sans (Apache 2.0) and Bitter (OFL).
