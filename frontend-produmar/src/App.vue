<script setup>
  import Sidebar from '@/components/Sidebar.vue'
  import Searchbar from '@/components/Searchbar.vue'
  import Overview from '@/components/Overview.vue'
</script>

<template>
  <div id="layout">
    <Sidebar />
    <div id="main">
      <Searchbar />
      <div id="content">
        <div>
          <Overview />
        </div>
        <div>
          
        </div>
      </div>
    </div>
  </div>

</template>

<style>
  html, body {
      padding: 0;
      margin: 0;
      font-family: Arial, Helvetica, sans-serif;
  }
  a {
      text-decoration: none;
      color: inherit;
  }
  #layout {
    display: flex;
    height: 100vh;
  }
  #main {
    flex: 1;
    display: flex;
    flex-direction: column;
  }
</style>
