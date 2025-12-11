let ws = new WebSocket("ws://" + location.host + "/ws");

// update values on each message
ws.onmessage = (event) => {
    let d = JSON.parse(event.data);

    rpmVal.textContent   = d.r.toFixed(0);
    speedVal.textContent = d.s.toFixed(0);
    thrVal.textContent   = d.t.toFixed(0);
    loadVal.textContent  = d.l.toFixed(0);

    hpVal.textContent    = d.h.toFixed(0);
    aggVal.textContent   = d.a.toFixed(0);

    z60Val.textContent   = d.z.toFixed(2);

    // Aggressiveness bar
    aggBar.style.width = d.a + "%";
    if (d.a < 30)  aggBar.style.background = "green";
    else if (d.a < 70) aggBar.style.background = "yellow";
    else aggBar.style.background = "red";
};

ws.onopen = () => console.log("WS connected");
ws.onclose = () => console.log("WS disconnected");
