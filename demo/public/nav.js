(function () {
  var page = document.body.getAttribute("data-nav-page");
  if (!page) return;
  document.querySelectorAll(".topnav [data-nav]").forEach(function (a) {
    if (a.getAttribute("data-nav") === page) {
      a.setAttribute("aria-current", "page");
      a.classList.add("is-active");
    }
  });
})();
