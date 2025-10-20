<script setup>
    import { RouterLink, RouterView } from 'vue-router'
    import logo from '@/assets/logo.png'
    import dashboardIcon from '@/assets/dashboardIcon.png'
    import medicaoIcon from '@/assets/medicaoIcon.png'
</script>

<template>
    <div id="sidebar">
        <picture>
            <RouterLink to=""><img :src="logo"></RouterLink>
        </picture>
        <section id="menu">
            <p>Menu Principal</p>
            <ol>
                <li><RouterLink to=""><img :src="dashboardIcon"> Dashboard</RouterLink></li>
                <li><RouterLink to=""><img :src="medicaoIcon"> Medição</RouterLink></li>
            </ol>
        </section>
    </div>
</template>

<style>
    #sidebar {
        width: 17vw;
        height: 100vh;
        border-right: 2px solid #E5E6EB;
    }
    #menu {
        width: 100%;
    }
    #menu p {
        color: #6C757A;
        margin-left: 12%;
    }
    #menu ol {
        list-style-type: none;
        color: #403D46;
    }
    #menu li {
        margin-bottom: 5%;
    }

</style>