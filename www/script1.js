document.addEventListener("DOMContentLoaded", () => {
  const STATUS_ENDPOINT = "/status";
  const FILES_ENDPOINT = "/api/files";
  const MEDIA_BASE_URL = "/downloads/";
  const REFRESH_INTERVAL = 3000; // 3 seconds

  const fileDropdown = document.getElementById("file-dropdown");
  const requestFileBtn = document.getElementById("request-file-btn");
  const mediaDisplay = document.getElementById("media-display");
  const statusIndicators = document.getElementById("status-indicators");
  const fileLogContent = document.getElementById("file-log-content");
  const dashboardStats = document.querySelector(".dashboard-stats");
  const threadGrid = document.getElementById("thread-grid");
  const poolSizeDisplay = document.getElementById("pool-size-display");
  let threadBoxes = []; 

  // --- Utility Function to Add Log Entry ---
  function addLogEntry(status, fileName, duration) {
    const time = new Date().toLocaleTimeString("en-US", { hour12: false });
    const entry = document.createElement("div");
    entry.className = "log-entry";

    let statusClass =
      status >= 200 && status < 300 ? "log-success" : "log-error";

    entry.innerHTML = `
            <span class="log-time">${time}</span>
            <span class="log-status ${statusClass}">${status}</span>
            <span class="log-file">${fileName}</span>
            <span class="log-duration">${duration.toFixed(2)}s</span>
        `;

    if (fileLogContent.firstChild) {
      fileLogContent.insertBefore(entry, fileLogContent.firstChild);
    } else {
      fileLogContent.appendChild(entry);
    }

    while (fileLogContent.children.length > 20) {
      fileLogContent.removeChild(fileLogContent.lastChild);
    }
  }

  // Thread Pool Grid Update Function ---
  function updateThreadGrid(totalThreads, activeThreads) {
    try {
      poolSizeDisplay.textContent = totalThreads;

      if (threadBoxes.length !== totalThreads) {
        threadGrid.innerHTML = ""; 
        threadBoxes = []; 
        for (let i = 0; i < totalThreads; i++) {
          const box = document.createElement("div");
          box.className = "thread-box";
          threadGrid.appendChild(box);
          threadBoxes.push(box); 
        }
      }

      //  Update the 'active' state of the existing boxes ---
      // (This runs every 2 seconds)
      for (let i = 0; i < threadBoxes.length; i++) {
        if (i < activeThreads) {
          // This thread is active
          threadBoxes[i].classList.add("active");
        } else {
          threadBoxes[i].classList.remove("active");
        }
      }
    } catch (error) {
      console.error("Error updating thread grid:", error);
      threadGrid.innerHTML = "";
      poolSizeDisplay.textContent = "Error";
      threadBoxes = [];
    }
  }

  // --- 1. Dashboard Status Update Function (Fetch every 2 seconds) ---
  async function fetchDashboardStatus() {
    const startTime = performance.now();
    let status = 0;
    
    try {
      const response = await fetch(STATUS_ENDPOINT);
      status = response.status;
      const endTime = performance.now();
      const duration = (endTime - startTime) / 1000;
    
      addLogEntry(status, "status", duration);

      if (!response.ok) {
        throw new Error(`HTTP error! status: ${response.status}`);
      }

      const data = await response.json();

      document.getElementById("activeThreads").textContent = data.activeThreads;

      const totalThreads = data.totalThreads || 32;

      // This updates the "Total Threads" stat box
      document.getElementById("totalThreads").textContent = totalThreads;

      document.getElementById("queuedTasks").textContent = data.queuedTasks;
      document.getElementById("totalRequests").textContent = data.totalRequests;
      document.getElementById("peakConnections").textContent =
        data.peakConnections;
      document.getElementById("closedConnections").textContent =
        data.closedConnections;

      // --- Update status indicators ---
      updateIndicators(data);

      // --- Call the new grid update function with the data ---
      updateThreadGrid(totalThreads, data.activeThreads);
    } catch (error) {
  
      // Log the error with the timer
      const endTime = performance.now();
      const duration = (endTime - startTime) / 1000;
      // We log 'status' (which is 0 if fetch failed, or the error code if it returned one)
      addLogEntry(status || 0, "status", duration);

      console.error("Error fetching dashboard status:", error);
      statusIndicators.innerHTML =
        '<span class="indicator danger">Server Offline</span>';
      updateThreadGrid(0, 0);
    }
  }


  function updateIndicators(data) {
    statusIndicators.innerHTML = "";

    const maxThreads = 32;

    // 1. Threads Indicator
    const activeThreads =
      data.activeThreads !== undefined ? data.activeThreads : 0;
    const activeIndicator = document.createElement("span");
    activeIndicator.textContent = `Threads: ${activeThreads}/${maxThreads}`;

    if (activeThreads >= maxThreads) {
      activeIndicator.className = "indicator danger";
    } else if (activeThreads > maxThreads * 0.75) {
      activeIndicator.className = "indicator warning";
    } else {
      activeIndicator.className = "indicator success";
    }
    statusIndicators.appendChild(activeIndicator);

    // 2. Queue Indicator - FIX APPLIED HERE: Ensure data.queuedTasks is not undefined
    const queuedTasks = data.queuedTasks !== undefined ? data.queuedTasks : 0;
    const queueIndicator = document.createElement("span");
    queueIndicator.textContent = `Queue: ${queuedTasks}`;

    if (queuedTasks > 5) {
      queueIndicator.className = "indicator danger";
    } else if (queuedTasks > 0) {
      queueIndicator.className = "indicator warning";
    } else {
      queueIndicator.className = "indicator success";
    }
    statusIndicators.appendChild(queueIndicator);
  }

  // --- 2. File List Population Function ---
  async function populateFileDropdown() {
    try {
      const response = await fetch(FILES_ENDPOINT);
      const files = await response.json();

      fileDropdown.innerHTML = '<option value="">Select a file...</option>';
      files.forEach((file) => {
        const option = document.createElement("option");
        option.value = file;
        option.textContent = file;
        fileDropdown.appendChild(option);
      });
    } catch (error) {
      console.error("Error fetching file list:", error);
      fileDropdown.innerHTML = '<option value="">Error fetching files</option>';
    }
  }

  // --- 3. File Request Handler (Simulates Load) ---
  requestFileBtn.addEventListener("click", async () => {
    const selectedFile = fileDropdown.value;
    if (!selectedFile) {
      alert("Please select a file first.");
      return;
    }

    // Reset layout to default grid while requesting
    dashboardStats.classList.remove("layout-vertical");

    mediaDisplay.innerHTML = `Requesting **${selectedFile}**...`;

    const startTime = performance.now(); 

    try {
      const response = await fetch(MEDIA_BASE_URL + selectedFile);
      const endTime = performance.now(); 
      const duration = (endTime - startTime) / 1000;

      // Log the request status and duration
      addLogEntry(response.status, selectedFile, duration);

      if (!response.ok) {
        throw new Error(
          `File not found or HTTP error! status: ${response.status}`
        );
      }

      // Successfully fetched - update the display
      mediaDisplay.innerHTML = ""; 

      dashboardStats.classList.add("layout-vertical");

      const fileExtension = selectedFile.split(".").pop().toLowerCase();

      if (["jpg", "jpeg", "png", "gif"].includes(fileExtension)) {
        const img = document.createElement("img");
        img.src = MEDIA_BASE_URL + selectedFile;
        mediaDisplay.appendChild(img);
      } else if (["mp4", "webm", "ogg"].includes(fileExtension)) {
        const video = document.createElement("video");
        video.src = MEDIA_BASE_URL + selectedFile;
        video.controls = true;
        video.autoplay = true;
        mediaDisplay.appendChild(video);
      } else {
        mediaDisplay.textContent = `File loaded: ${selectedFile} (Unknown type)`;

        // Don't use stack layout for simple text
        dashboardStats.classList.remove("layout-vertical");
      }
    } catch (error) {
      const endTime = performance.now();
      const duration = (endTime - startTime) / 1000;
      addLogEntry(0, selectedFile, duration);
      // Ensure layout is reset on error
      dashboardStats.classList.remove("layout-vertical");

      console.error("Error requesting file:", error);
      mediaDisplay.innerHTML = `<p style="color: red;">Error: ${error.message}</p>`;
    }
  });

  // --- Initialization ---
  fetchDashboardStatus();
  setInterval(fetchDashboardStatus, REFRESH_INTERVAL);
  populateFileDropdown();
});
