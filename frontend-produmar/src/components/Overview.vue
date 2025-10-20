<script setup>
    import { ref } from 'vue'
    import overviewCard1 from '@/assets/overviewCard1.png'
    import overviewCard2 from '@/assets/overviewCard2.png'
    import overviewCard3 from '@/assets/overviewCard3.png'
    import overviewCard4 from '@/assets/overviewCard4.png'
    const overview_values = ref([1, 2, 3, 4])
</script>

<template>
    <div id="overview">
        <div id="overview-title">
            <h1>Dashboard</h1>
            <p>Visão geral da central de monitoramento</p>
        </div>
        <div id="overview-cards">
            <div class="card">
                <div class="card-title">
                    <strong>Embarcações monitoradas</strong>
                    <img :src="overviewCard1">
                </div>
                <strong class="overview-values">{{ overview_values[0] }}</strong>
            </div>
            <div class="card">
                <div class="card-title">
                    <strong>Embarcações em atividade </strong>
                    <img :src="overviewCard2">
                </div>
                <strong class="overview-values">{{ overview_values[1] }}</strong>
            </div>
            <div class="card">
                <div class="card-title">
                    <strong>Alertas</strong>
                    <img :src="overviewCard3">                    
                </div>
                <strong class="overview-values">{{ overview_values[2] }}</strong>
            </div>
            <div class="card">
                <div class="card-title">
                    <strong>Média de RPM</strong>
                    <img :src="overviewCard4">
                </div>
                <strong class="overview-values">{{ overview_values[3] }} RPM</strong>
            </div>
        </div>
    </div>
</template>

<style scoped>
    #overview {
        display: flex;
        flex-direction: column;
        align-items: baseline;
        
    }
    #overview-title {
        margin-left: 1em;
    }
    #overview-cards {
        display: flex;
        gap: 20px;
    }
    .card-title {
        display: flex;
        justify-content: space-between;
        
    }
    .card {
        height: 10vh;
        width: 17vw;
        border: 1px solid #F3F3F3;    
        border-radius: 3%;
        display: flex;
        flex-direction: column; 
    }
    .overview-values {
        font-size: 30px;
    }
    .card-title, .overview-values {
        margin: 0.5em;
    }
</style>