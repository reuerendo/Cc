# CONNECT TO CALIBRE
An application for wireless transfer of books from Calibre to PocketBook[^1] devices. It also allows for the synchronization of reading status and date, as well as collections and favorites.
[^1]: The program was tested on a PocketBook InkPad 4. 
## Installation:
Place the file `connect-to-calibre.app` in the `/applications` folder on your PocketBook device. If desired, also copy the `/icons` folder ([instructions on how to assign an application icon](https://github.com/jjrrw174/PocketBook-Desktop-and-App-Customizations)).
## Usage
Launch Calibre first, then run `connect-to-calibre.app`. The application will automatically connect to Calibre upon startup.
When you first connect, scanning your device's library will take a few minutes. On subsequent connections, the app will rely on the cache and become significantly faster.
## Read status and Favorite
The app also supports read status and favorite marking. In the settings, you need to specify the lookup name for the `Read`, `Read Date`, and `Favorite` columns. `Read` and `Favorite` columns should be of the `yes/no` type.
## Collections
The app can add books to collections in the PocketBook library. To use this function, you should specify the column lookup name in the Calibre (in the `#name` format) in the SmartDevice App Interface settings.

<img src="https://github.com/reuerendo/pocketbook-db-calibre.koplugin/blob/main/col.png" width="445">

*Please make a backup copy of your Calibre database*
