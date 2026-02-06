/**
 * Copyright (c) @CompanyNameMagicTag 2023-2023. All rights reserved.
 */

package android.nearlink;

/**
 * Nearlink帮助类
 *
 * @since 2023-12-04
 */
public class NearlinkUtils {

    static void checkAdapterStateOn(NearlinkAdapter adapter) {

        if (adapter == null) {
            throw new IllegalStateException("NearlinkAdapter is null");
        }

        if (!adapter.isEnabled()) {
            throw new IllegalStateException("Nearlink Adapter is not turned ON");
        }
    }

}
