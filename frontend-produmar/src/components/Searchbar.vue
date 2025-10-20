<script setup>
    import searchIcon from '@/assets/searchIcon.png'
    import filterIcon from '@/assets/filterIcon.png'
</script>

<template>
    <div id="searchbar">
        <form action="" method="">
            <div id="searchbar-box">
                <div id="search-box-input">
                    <button type="submit" id="search-submit"><img :src="searchIcon"></button>
                    <input type="text" placeholder="Pesquisar..." id="search-input">
                </div>
                <button type="" id="filter-submit"><img :src="filterIcon"></button>
            </div>
        </form>
    </div>
</template>


<style scoped>
    #searchbar {
        height: 8vh;
        background-color: white;
        border-bottom: 2px solid #E5E6EB;
    }
    #searchbar-box {
        margin-left: 1em;
        display: flex;
        align-items: center;
        justify-content:space-between;
        height: 8vh;
    }
    #filter-submit {
        background: none;
        border: none;
        margin-right: 1em;
    }
    #search-box-input {
        display: flex;
        align-items: center;
        background-color: #FAFAFA;
        height: 40%;
        width: 25%;
    }
    #search-input, #search-submit {
        background: none;
        border: none;
    }
</style>