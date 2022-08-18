const char HTML_MAIN[] PROGMEM = R"rawliteral(
<figure class="text-center"><h2 id="devicename"></h2></figure>
<div class="row gx-0 mb-2">
<div class="col">
<div class="bg-light">Battery: </div>
</div>
<div class="col">
<div class="bg-light"><span id="battV" >N/A</span><span id="packA" >N/A</span><span id="packSOC" >N/A</span></div>
</div>
</div>
<div class="row gx-0 mb-2">
<div class="col">
<div class="bg-light">Remaining Capacity: </div>
</div>
<div class="col">
<div class="bg-light"><span id="packRes">N/A</span></div>
</div>
</div>
<div class="row gx-0 mb-2">
<div class="col">
<div class="bg-light">Charge Cycles: </div>
</div>
<div class="col">
<div class="bg-light"><span id="packCycles">N/A</span></div>
</div>
</div>
<div class="row gx-0 mb-2">
<div class="col">
<div class="bg-light">Temperature: </div>
</div>
<div class="col">
<div class="bg-light"><span id="packTemp">N/A</span></div>
</div>
</div>
<div class="row gx-0 mb-2">
<div class="col">
<div class="bg-light">Cells Hi/Lo/Diff: </div>
</div>
<div class="col">
<div class="bg-light"><span id="cellH">N/A</span><span id="cellL">N/A</span><span id="cellDiff">N/A</span></div>
</div>
</div>

<div class="row gx-0 mb-2">
<div class="col">
<div class="bg-light">Load State: </div>
</div>
<div class="col">
<div class="bg-light form-check form-switch"><!--<span id="loadState">N/A</span>--><input class="form-check-input" type="checkbox" onchange="toggleLoadState(this)" role="switch" id="loadState" /></div>
</div>
</div>


<div class="d-grid gap-2">
<a class="btn btn-primary btn-block" href="/settings" role="button">Settings</a>
</div>
<script>
        $(document).ready(function(load) {
         function fetch() {
        $.ajax({
            url: "livejson",
            data: {},
            type: "get",
            dataType: "json",
               cache: false,
                success: function (data) {
               document.getElementById("battV").innerHTML = data.LiveData.BATT_VOLTS +'V ';
               document.getElementById("packA").innerHTML = data.packA+'A  ';
               document.getElementById("packSOC").innerHTML = data.BATTERY_SOC+'%%';
               document.getElementById("packRes").innerHTML = data.packRes+'mAh ';
               document.getElementById("packCycles").innerHTML = data.packCycles+' ';
               document.getElementById("packTemp").innerHTML = data.packTemp+'°C ';
               document.getElementById("cellH").innerHTML = data.cellH+'V ';
               document.getElementById("cellL").innerHTML = data.cellL+'V ';
               document.getElementById("cellDiff").innerHTML = data.cellDiff+'mV ';
               document.getElementById("loadState").checked = data.LOAD_STATE;
               document.getElementById("devicename").innerHTML = 'Device: '+data.device_name;
            }
        });
        }
        setInterval(fetch, 5000);
        fetch();
        });
function toggleLoadState(element) {
var xhr = new XMLHttpRequest();
if(element.checked){ xhr.open("GET", "/set?loadstate=1", true); }
else { xhr.open("GET", "/set?loadstate=0", true); }
xhr.send();
clearInterval();
}
</script>
)rawliteral";