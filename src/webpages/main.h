const char HTML_MAIN[] PROGMEM = R"rawliteral(
<figure class="text-center"><h2 id="devicename"></h2></figure>

<div class="row gx-0 mb-2">
    <div class="col">
        <div class="bg-light">Device Time: </div>
    </div>
    <div class="col">
        <div class="bg-light"><span id="devtime" >N/A</span></div>
    </div>
</div>

<div class="row gx-0 mb-2">
    <div class="col">
        <div class="bg-light">Solar: </div>
    </div>
    <div class="col">
        <div class="bg-light"><span id="solarV" >N/A</span><span id="solarA" >N/A</span><span id="solarW" >N/A</span></div>
    </div>
</div>

<div class="row gx-0 mb-2">
    <div class="col">
        <div class="bg-light">Battery: </div>
    </div>
    <div class="col">
        <div class="bg-light"><span id="battV" >N/A</span><span id="battA" >N/A</span><span id="battW" >N/A</span><span id="battSOC" >N/A</span></div>
    </div>
</div>

<div class="row gx-0 mb-2">
    <div class="col">
        <div class="bg-light">Load: </div>
    </div>
    <div class="col">
        <div class="bg-light"><span id="loadV" >N/A</span><span id="loadA" >N/A</span><span id="loadW" >N/A</span></div>
    </div>
</div>

<div class="row gx-0 mb-2">
    <div class="col">
        <div class="bg-light">Consumed Kwh: </div>
    </div>
    <div class="col">
        <div class="bg-light"><span id="consD" >N/A</span><span id="consM" >N/A</span><span id="consY" >N/A</span><span id="consT" >N/A</span></div>
    </div>
</div>

<div class="row gx-0 mb-2">
    <div class="col">
        <div class="bg-light">generated Kwh: </div>
    </div>
    <div class="col">
        <div class="bg-light"><span id="genD" >N/A</span><span id="genM" >N/A</span><span id="genY" >N/A</span><span id="genT" >N/A</span></div>
    </div>
</div>

<div class="row gx-0 mb-2">
    <div class="col">
        <div class="bg-light">CO2 Reduction: </div>
    </div>
    <div class="col">
        <div class="bg-light"><span id="cored" >N/A</span></div>
    </div>
</div>

<div class="row gx-0 mb-2">
    <div class="col">
        <div class="bg-light">Input State: </div>
    </div>
    <div class="col">
        <div class="bg-light"><span id="inputstate" >N/A</span></div>
    </div>
</div>

<div class="row gx-0 mb-2">
    <div class="col">
        <div class="bg-light">Charge Mode: </div>
    </div>
    <div class="col">
        <div class="bg-light"><span id="chrgmode" >N/A</span></div>
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
               document.getElementById("devicename").innerHTML = 'Device: '+data.DEVICE_NAME;

               document.getElementById("devtime").innerHTML = data.DEVICE_TIME;

               document.getElementById("solarV").innerHTML = data.LiveData.SOLAR_VOLTS +'V ';
               document.getElementById("solarA").innerHTML = data.LiveData.SOLAR_AMPS+'A  ';
               document.getElementById("solarW").innerHTML = data.LiveData.SOLAR_WATTS+'W  ';
               document.getElementById("battSOC").innerHTML = data.LiveData.BATTERY_SOC+'%%';

               document.getElementById("battV").innerHTML = data.LiveData.BATT_VOLTS +'V ';
               document.getElementById("battA").innerHTML = data.LiveData.BATT_AMPS+'A  ';
               document.getElementById("battW").innerHTML = data.LiveData.BATT_WATTS+'W  ';

               document.getElementById("loadV").innerHTML = data.LiveData.LOAD_VOLTS +'V ';
               document.getElementById("loadA").innerHTML = data.LiveData.LOAD_AMPS+'A  ';
               document.getElementById("loadW").innerHTML = data.LiveData.LOAD_WATTS+'W  ';

               document.getElementById("consD").innerHTML = 'D:'+data.StatsData.CONS_ENERGY_DAY+'  ';
               document.getElementById("consM").innerHTML = 'M:'+data.StatsData.CONS_ENGERY_MON+'  ';
               document.getElementById("consY").innerHTML = 'Y:'+data.StatsData.CONS_ENGERY_YEAR+'  ';
               document.getElementById("consT").innerHTML = 'T:'+data.StatsData.CONS_ENGERY_TOT;

               document.getElementById("genD").innerHTML = 'D:'+data.StatsData.GEN_ENERGY_DAY+'  ';
               document.getElementById("genM").innerHTML = 'M'+data.StatsData.GEN_ENERGY_MON+'  ';
               document.getElementById("genY").innerHTML = 'Y:'+data.StatsData.GEN_ENERGY_YEAR+'  ';
               document.getElementById("genT").innerHTML = 'T:'+data.StatsData.GEN_ENERGY_TOT;

               document.getElementById("cored").innerHTML = data.StatsData.CO2_REDUCTION+'t ';

               document.getElementById("inputstate").innerHTML = data.CHARGER_INPUT_STATUS;
               document.getElementById("chrgmode").innerHTML = data.CHARGER_MODE;

               document.getElementById("loadState").checked = data.LOAD_STATE;
            }
        });
        }
        setInterval(fetch, 2000);
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