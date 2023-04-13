var datainfo;

get_info();

var interval = setInterval(get_info, 5000);

function get_info(){
	try{
	    $.ajax({
                url: "getinfo.php",
                type: "POST",
                data: "data",
                success: function (data) {
		    datainfo = JSON.parse(data);
		    if($("#sfp_panel").length > 0)
			updateValues();
		    else
			clearInterval(interval);
                },
                error: function () {
		    clearInterval(interval);
                }
            }); // AJAX Get
	}
	catch(err){
		console.log(err);
	}
}

function updateValues(){
	$("#sfp_panel").html(datainfo[2]);
	datainfo[0].shift();
	$("#temp").text(datainfo[0].join(" "));
	$("#datewr").html(datainfo[1][0] + '<br>' + datainfo[1][1]);

	var status;
	var end = false;
	for (var i=0; i<datainfo[3].length-1 && !end; i++){
		var content = datainfo[3][i];
		if (content.indexOf("sv")>-1){
			//datainfo[3][i] = '<b>' + content + '<b><br/>';
			if (content.indexOf("0")>-1) {
				datainfo[3][i+1] = '' + '<br/>';//modify next line
				status = datainfo[3][i+1];
			}
			else{
				datainfo[3][i+1] = datainfo[3][i+1].replace("ss:", "");
				datainfo[3][i+1] = datainfo[3][i+1].replace("\'", "");
				datainfo[3][i+1] = datainfo[3][i+1].replace("\'", "");
				datainfo[3][i+1] = datainfo[3][i+1] ;
				status = datainfo[3][i+1];
			}
			end=true; //jump to next state
		}
	}

	$("#timing").html(status);
}


