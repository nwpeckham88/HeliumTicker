#include <pgmspace.h>
char index_html[] PROGMEM = R"=====(
<!doctype html>
<html lang='en' dir='ltr'>
<head>
  <meta http-equiv='Content-Type' content='text/html; charset=utf-8' />
  <meta name='viewport' content='width=device-width, initial-scale=1.0' />
  <title>Helium Ticker</title>
  <script type='text/javascript'>
    var activeButton = null;
    var colorCanvas = null;
    
    window.addEventListener('DOMContentLoaded', (event) => {
          // init the canvas color picker
      colorCanvas = document.getElementById('color-canvas');
      var colorctx = colorCanvas.getContext('2d');
    
      // Create color gradient
      var gradient = colorctx.createLinearGradient(0, 0, colorCanvas.width - 1, 0);
      gradient.addColorStop(0,    "rgb(255,   0,   0)");
      gradient.addColorStop(0.16, "rgb(255,   0, 255)");
      gradient.addColorStop(0.33, "rgb(0,     0, 255)");
      gradient.addColorStop(0.49, "rgb(0,   255, 255)");
      gradient.addColorStop(0.66, "rgb(0,   255,   0)");
      gradient.addColorStop(0.82, "rgb(255, 255,   0)");
      gradient.addColorStop(1,    "rgb(255,   0,   0)");
    
      // Apply gradient to canvas
      colorctx.fillStyle = gradient;
      colorctx.fillRect(0, 0, colorCanvas.width - 1, colorCanvas.height - 1);
    
      // Create semi transparent gradient (white -> transparent -> black)
      gradient = colorctx.createLinearGradient(0, 0, 0, colorCanvas.height - 1);
      gradient.addColorStop(0,    "rgba(255, 255, 255, 1)");
      gradient.addColorStop(0.48, "rgba(255, 255, 255, 0)");
      gradient.addColorStop(0.52, "rgba(0,     0,   0, 0)");
      gradient.addColorStop(1,    "rgba(0,     0,   0, 1)");
    
      // Apply gradient to canvas
      colorctx.fillStyle = gradient;
      colorctx.fillRect(0, 0, colorCanvas.width - 1, colorCanvas.height - 1);
  
      
      // setup the canvas click listener
      colorCanvas.addEventListener('click', (event) => {
        var imageData = colorCanvas.getContext('2d').getImageData(event.offsetX, event.offsetY, 1, 1);
    
        var selectedColor = 'rgb(' + imageData.data[0] + ',' + imageData.data[1] + ',' + imageData.data[2] + ')'; 
        //console.log('click: ' + event.offsetX + ', ' + event.offsetY + ', ' + selectedColor);
        document.getElementById('color-value').value = selectedColor;
    
        selectedColor = imageData.data[0] * 65536 + imageData.data[1] * 256 + imageData.data[2];
        submitVal('c', selectedColor);
      });

      // Load all the stats to display. We use the checkbox id to refer to the "stat" throughout the code. The results are hardcoded for now.
      for (let stat of document.getElementsByClassName("stat")) {
        getStat(stat);
      }
      
    });
    
    function initMode(mode, index) {
      mode.addEventListener('click', (event) => onMode(event, index));
    }
    
    function onColor(event, color) {
      event.preventDefault();
      var match = color.match(/rgb\(([0-9]*),([0-9]*),([0-9]*)\)/);
      if(match) {
        var colorValue = Number(match[1]) * 65536 + Number(match[2]) * 256 + Number(match[3]);
        //console.log('onColor:' + match[1] + "," + match[2] + "," + match[3] + "," + colorValue);
        submitVal('c', colorValue);
      }
    }
    
    function onBrightness(event, dir) {
      event.preventDefault();
      submitVal('b', dir);
    }
        
    function submitVal(name, val) {
      var xhttp = new XMLHttpRequest();
      xhttp.open('GET', 'set?' + name + '=' + val, true);
      xhttp.send();
    }
    
    function checkboxString(checked){
      if(checked){
        return "true";
      } else {
        return "false";
      }
    }
    
    function onChangeStat(event, checkbox) {
      event.preventDefault();
      let req = new XMLHttpRequest();
      req.open('GET', "set?" + checkbox.id + "&status=" + checkboxString(checkbox.checked));
      req.onload = function() {
        if (req.status == 200) {
          if (this.responseText == "true") {
            checkbox.checked = true;
          } else {
            checkbox.checked = false;
          }
        } else {
          checkbox.checked = "false";
        }
      }
      req.send();
    }
    
    function getStat(stat) {
      event.preventDefault();
      let req = new XMLHttpRequest();
      req.open('GET', "getStat?" + stat.id);
      req.onload = function() {
        if (req.status == 200) {
          if (this.responseText == "true") {
            stat.checked = true;
          } else {
            stat.checked = false;
          }
        } else {
          stat.checked = "false";
        }
      }
      req.send();
    }
    
  </script>

  <style>
    body {
      font-family:Arial,sans-serif;
      margin:10px;
      padding:0;
      background-color:#202020;
      color:#909090;
      text-align:center;
    }

    .flex-row {
      display:flex;
      flex-direction:row;
    }

    .flex-row-wrap {
      display:flex;
      flex-direction:row;
      flex-wrap:wrap;
    }

    .flex-col {
      display:flex;
      flex-direction:column;
      align-items:center;
    }

    input[type='text'] {
      background-color: #d0d0d0;
      color:#404040;
    }

    ul {
      list-style-type: none;
    }

    ul li a {
      display:block;
      margin:3px;
      padding:10px;
      border:2px solid #404040;
      border-radius:5px;
      color:#909090;
      text-decoration:none;
    }

    ul#modes li a {
      min-width:220px;
    }
    
    ul.control li a {
      min-width:60px;
      min-height:24px;
    }

    ul.control {
      display:flex;
      flex-direction:row;
      justify-content: flex-end;
      align-items: center;
      padding: 0px;
    }

    ul li a.active {
      border:2px solid #909090;
      background:#4A4A4A;
    }

    ul li a.
  </style>
</head>
<body>
  <h1>Helium Ticker Control</h1>
  <div class='flex-row'>
  <div class='flex-col'>
  <div><canvas id='color-canvas' width='360' height='360'></canvas><br/></div>
  <div><input type='text' id='color-value' value='0xFF5900' oninput='onColor(event, this.value)'/></div>

  <div>
    <ul class='control'>
      <li>Brightness:</li>
      <li><a href='#' onclick="onBrightness(event, '-')">&#9788;</a></li>
      <li><a href='#' onclick="onBrightness(event, '+')">&#9728;</a></li>
    </ul>

    <ul class='control' id='stats-form'>
      <li>Data:</li>
      <li><input type="checkbox" id="daily-average" name="daily-average" class="stat" value="Daily Average" oninput='onChangeStat(event, this)'><label for="daily-avg">24HR HNT Avg</label><br></li>
      <li><input type="checkbox" id="total" name="total" class="stat" value="Total" oninput='onChangeStat(event, this)'><label for="total">Wallet Total</label><br></li>
      <li><input type="checkbox" id="witnesses" name="witnesses" class="stat" value="Witnesses" oninput='onChangeStat(event, this)'><label for="witnesses">Witnesses</label><br></li>
      <li><input type="checkbox" id="thirty-day-total" name="thirty-day-total" class="stat" value="Thirty Day Total" oninput='onChangeStat(event, this)'><label for="thirty-day-total">30-Day Total</label><br></li>
    </ul>
  </div>
  </div>

    <div>
      <ul id='modes' class='flex-row-wrap'>
    </div>
  </div>
</body>
</html>
)=====";
