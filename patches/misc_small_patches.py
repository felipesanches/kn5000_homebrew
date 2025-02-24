def misc_small_patches__patch(maincpu_patches):
    pass

## FIX THIS!!!
##        maincpu_patches[0xFB7848] = [0x66]  # para passar de um teste com falha de comunicação com os MCUs do painel de controle

        #FB7843 = 6E # para não mostrar o erro de comm. inter cpu
 
##        maincpu patches:
##        EF3393 = 0E # um RET para terminar mais rápido a carga (sem sucesso) de payload para a subCPU
##        FB7863 = E0 # para mostrar a tela de seleção de demonstrações musicais
##        FB786B = D7, FA, 05, 0E # para não mostrar o erro de comm. inter cpu
        
        # não funciona:  maincpu_patches[0xFEBF91] = [0x6E]
        # PC=f5535d 
        # PC=ef125f
        # PC=f559a8
        # PC=ef15cb
        # PC=ef2a6b
##        pc=f5ab2f

        # 0xFB729E # "MainCPU_self_test_routines"
        # 0xFB7328 # "A_Short_Pause"
